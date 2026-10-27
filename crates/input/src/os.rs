//! Thin OS glue over `rdev` (ADR-0005). No decisions are made here: events
//! are converted and handed to the pure engines.
// Host mode is stubbed on Linux (not supported yet), so helpers are unused there.
#![cfg_attr(
    not(any(target_os = "macos", target_os = "windows")),
    allow(dead_code, unused_imports)
)]

use crate::guest::Act;
use crate::host::{Ev, HostEngine, Step};
use crate::layout::{PeerId, Size};
use anyhow::{anyhow, Result};
use mono_proto::Msg;
use rdev::EventType;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::sync::mpsc::UnboundedSender;

/// Size of the main screen in the coordinate space the OS uses for pointer events.
///
/// On macOS, rdev's `display_size` returns PIXELS (3024x1964 on a Retina MacBook),
/// but mouse events and CGEvent positions use POINTS (1512x982). Using pixels made
/// every edge and every absolute position on a Mac wrong by the scale factor, so
/// the Mac path reads the main display bounds in points instead.
pub fn screen_size() -> Result<Size> {
    #[cfg(target_os = "macos")]
    {
        #[repr(C)]
        #[derive(Clone, Copy)]
        struct CGPoint {
            x: f64,
            y: f64,
        }
        #[repr(C)]
        #[derive(Clone, Copy)]
        struct CGSize {
            width: f64,
            height: f64,
        }
        #[repr(C)]
        #[derive(Clone, Copy)]
        struct CGRect {
            origin: CGPoint,
            size: CGSize,
        }
        #[link(name = "CoreGraphics", kind = "framework")]
        extern "C" {
            fn CGMainDisplayID() -> u32;
            fn CGDisplayBounds(display: u32) -> CGRect;
        }
        // SAFETY: plain CoreGraphics queries with a valid display id.
        let r = unsafe { CGDisplayBounds(CGMainDisplayID()) };
        if r.size.width > 0.0 && r.size.height > 0.0 {
            return Ok(Size {
                w: r.size.width,
                h: r.size.height,
            });
        }
    }
    let (w, h) = rdev::display_size().map_err(|e| anyhow!("display size: {e:?}"))?;
    Ok(Size {
        w: w as f64,
        h: h as f64,
    })
}

fn simulate(e: &EventType) -> Result<()> {
    rdev::simulate(e).map_err(|_| anyhow!("input injection failed for {e:?}"))
}

/// Moves the pointer to (x, y) and returns where the OS left it. On Windows the
/// move is `SetCursorPos` (what rdev's MouseMove calls there); a refused move is
/// always an error. The landing is read back with `GetCursorPos` only when `check`
/// is set (the trace window after a crossing), and a landing more than 2 px from
/// the request is then an error that says where the pointer is. Elsewhere the move
/// is made and counts as landed.
pub fn warp(x: f64, y: f64, check: bool) -> Result<(f64, f64), String> {
    #[cfg(windows)]
    {
        use winapi::shared::windef::POINT;
        use winapi::um::winuser::{GetCursorPos, SetCursorPos};
        // SAFETY: plain call with two integer arguments.
        if unsafe { SetCursorPos(x as i32, y as i32) } == 0 {
            return Err("SetCursorPos refused the move".to_string());
        }
        if !check {
            return Ok((x, y));
        }
        let mut p = POINT { x: 0, y: 0 };
        // SAFETY: `p` is a valid out-structure for GetCursorPos.
        if unsafe { GetCursorPos(&mut p) } == 0 {
            return Err("GetCursorPos failed".to_string());
        }
        let (ax, ay) = (f64::from(p.x), f64::from(p.y));
        if (ax - x).abs() > 2.0 || (ay - y).abs() > 2.0 {
            return Err(format!("OS at ({ax:.0},{ay:.0}), not ({x:.0},{y:.0})"));
        }
        Ok((ax, ay))
    }
    #[cfg(not(windows))]
    {
        // Only Windows reads the landing back (see the doc above).
        let _ = check;
        simulate(&EventType::MouseMove { x, y }).map_err(|e| format!("{e:#}"))?;
        Ok((x, y))
    }
}

/// One log line for a warp to `(x, y)` and its result.
fn warp_line((x, y): (f64, f64), res: &Result<(f64, f64), String>) -> String {
    match res {
        Ok((ax, ay)) => format!("warp to ({x:.0},{y:.0}) -> OS at ({ax:.0},{ay:.0})"),
        Err(e) => format!("warp failed: {e}"),
    }
}

/// Warps logged in full after each crossing, so the first run shows whether the OS
/// takes them.
const WARP_TRACE: u8 = 8;
/// Smallest gap between two warp-failure notes once the trace has run out.
const WARP_FAIL_EVERY: Duration = Duration::from_secs(2);

/// Chooses which warp results reach the log: every one of the first `WARP_TRACE`
/// warps after a crossing, then failures only, at most one per `WARP_FAIL_EVERY`.
#[derive(Default)]
pub struct WarpLog {
    /// Warps made since the last crossing (saturates).
    since_crossing: u8,
    last_fail: Option<Instant>,
}

impl WarpLog {
    /// The cursor just crossed to the other computer: trace the next warps again.
    pub fn crossed(&mut self) {
        self.since_crossing = 0;
    }

    /// True while the next warp is in the trace window, so its landing is read back.
    pub fn tracing(&self) -> bool {
        self.since_crossing < WARP_TRACE
    }

    /// The log line for one warp to `(x, y)` with result `res`, if it is logged.
    pub fn note(
        &mut self,
        target: (f64, f64),
        res: &Result<(f64, f64), String>,
        now: Instant,
    ) -> Option<String> {
        let traced = self.tracing();
        self.since_crossing = self.since_crossing.saturating_add(1);
        if traced {
            if res.is_err() {
                self.last_fail = Some(now);
            }
            return Some(warp_line(target, res));
        }
        if res.is_ok() || !due(self.last_fail, now, WARP_FAIL_EVERY) {
            return None;
        }
        self.last_fail = Some(now);
        Some(warp_line(target, res))
    }
}

/// Replay one action on this machine.
pub fn inject(a: &Act) -> Result<()> {
    simulate(&match *a {
        Act::MoveTo(x, y) => EventType::MouseMove { x, y },
        Act::Key { key, down: true } => EventType::KeyPress(key),
        Act::Key { key, down: false } => EventType::KeyRelease(key),
        Act::Button { button, down: true } => EventType::ButtonPress(button),
        Act::Button {
            button,
            down: false,
        } => EventType::ButtonRelease(button),
        Act::Wheel { dx, dy } => EventType::Wheel {
            delta_x: dx,
            delta_y: dy,
        },
    })
}

fn convert(t: &EventType) -> Option<Ev> {
    Some(match *t {
        EventType::KeyPress(key) => Ev::Key { key, down: true },
        EventType::KeyRelease(key) => Ev::Key { key, down: false },
        EventType::ButtonPress(button) => Ev::Button { button, down: true },
        EventType::ButtonRelease(button) => Ev::Button {
            button,
            down: false,
        },
        EventType::MouseMove { x, y } => Ev::Move { x, y },
        EventType::Wheel { delta_x, delta_y } => Ev::Wheel {
            dx: delta_x,
            dy: delta_y,
        },
    })
}

/// Carries out one step. The warp happens before any message is sent, so the
/// peer never hears Enter while the host cursor is still on the edge (ADR-0018 (e)).
fn apply(step: &Step, tx: &UnboundedSender<(PeerId, Msg)>, warps: &mut WarpLog) -> Option<String> {
    if step
        .send
        .iter()
        .any(|(_, m)| matches!(m, Msg::Enter { .. }))
    {
        warps.crossed();
    }
    let note = step.warp.and_then(|(x, y)| {
        let res = warp(x, y, warps.tracing());
        warps.note((x, y), &res, Instant::now())
    });
    for (p, m) in &step.send {
        let _ = tx.send((*p, m.clone()));
    }
    note
}

#[cfg(target_os = "macos")]
mod perm {
    #[link(name = "ApplicationServices", kind = "framework")]
    extern "C" {
        fn AXIsProcessTrusted() -> bool;
    }
    #[link(name = "CoreGraphics", kind = "framework")]
    extern "C" {
        fn CGPreflightListenEventAccess() -> bool;
        fn CGRequestListenEventAccess() -> bool;
        fn CGRequestPostEventAccess() -> bool;
    }
    pub fn accessibility() -> bool {
        // SAFETY: plain C query with no arguments.
        unsafe { AXIsProcessTrusted() }
    }
    pub fn input_monitoring() -> bool {
        // SAFETY: plain C query with no arguments.
        unsafe { CGPreflightListenEventAccess() }
    }
    pub fn request_accessibility() {
        // SAFETY: plain C call; it only shows the system permission prompt.
        unsafe {
            CGRequestPostEventAccess();
        }
    }
    pub fn request_input_monitoring() {
        // SAFETY: plain C call; it only shows the system permission prompt and
        // adds the app to the Input Monitoring list.
        unsafe {
            CGRequestListenEventAccess();
        }
    }
}

pub const ACCESSIBILITY: &str = "Accessibility";
pub const INPUT_MONITORING: &str = "Input Monitoring";

/// Permissions macOS still withholds (always empty elsewhere).
pub fn missing_permissions() -> Vec<&'static str> {
    #[cfg(target_os = "macos")]
    {
        let mut v = vec![];
        if !perm::accessibility() {
            v.push(ACCESSIBILITY);
        }
        if !perm::input_monitoring() {
            v.push(INPUT_MONITORING);
        }
        v
    }
    #[cfg(not(target_os = "macos"))]
    {
        vec![]
    }
}

pub fn permissions_granted() -> bool {
    missing_permissions().is_empty()
}

/// Ask macOS for one permission (adds the app to its Settings list and shows
/// the prompt). Ask for one at a time: a second request made right after the
/// first is ignored, which left the app missing from the Input Monitoring list.
pub fn request_permission(name: &str) {
    #[cfg(target_os = "macos")]
    {
        if name == INPUT_MONITORING {
            perm::request_input_monitoring();
        } else {
            perm::request_accessibility();
        }
    }
    #[cfg(not(target_os = "macos"))]
    let _ = name;
}

/// Open the System Settings page where `name` is switched on (macOS).
pub fn open_permission_settings(name: &str) {
    #[cfg(target_os = "macos")]
    {
        let pane = if name == INPUT_MONITORING {
            "Privacy_ListenEvent"
        } else {
            "Privacy_Accessibility"
        };
        let _ = std::process::Command::new("open")
            .arg(format!(
                "x-apple.systempreferences:com.apple.preference.security?{pane}"
            ))
            .spawn();
    }
    #[cfg(not(target_os = "macos"))]
    let _ = name;
}

/// Windows only: is the pointer drawn right now? `None` when unknown (not Windows, or
/// the query failed).
pub fn cursor_visibility() -> Option<bool> {
    #[cfg(windows)]
    {
        use winapi::um::winuser::{GetCursorInfo, CURSORINFO, CURSOR_SHOWING};
        let mut ci = CURSORINFO {
            cbSize: std::mem::size_of::<CURSORINFO>() as u32,
            flags: 0,
            hCursor: std::ptr::null_mut(),
            ptScreenPos: winapi::shared::windef::POINT { x: 0, y: 0 },
        };
        // SAFETY: `ci` is a valid out-structure with cbSize set.
        if unsafe { GetCursorInfo(&mut ci) } != 0 {
            return Some(ci.flags & CURSOR_SHOWING != 0);
        }
    }
    None
}

/// One line describing the monitors, for the log (Windows only).
pub fn display_info() -> String {
    #[cfg(windows)]
    {
        use winapi::um::winuser::*;
        // SAFETY: plain value queries.
        unsafe {
            format!(
                "monitors={} primary={}x{} virtual={}x{} at ({},{})",
                GetSystemMetrics(SM_CMONITORS),
                GetSystemMetrics(SM_CXSCREEN),
                GetSystemMetrics(SM_CYSCREEN),
                GetSystemMetrics(SM_CXVIRTUALSCREEN),
                GetSystemMetrics(SM_CYVIRTUALSCREEN),
                GetSystemMetrics(SM_XVIRTUALSCREEN),
                GetSystemMetrics(SM_YVIRTUALSCREEN)
            )
        }
    }
    #[cfg(not(windows))]
    String::new()
}

/// What `CursorKeeper` changed on this computer, for the log.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CursorChange {
    /// MouseKeys was switched on so Windows draws the pointer.
    Enabled,
    /// The MouseKeys settings the user had were put back.
    Restored,
    /// Windows refused a change (reported once per episode).
    Failed,
    /// MouseKeys or hide-while-typing had been switched back by Windows or another
    /// program and was put back (noted at most every 10 s).
    Reasserted,
}

/// Makes Windows draw the pointer while the cursor is on this computer and no mouse
/// is attached. Windows draws no pointer without a mouse, so this does what Input
/// Leap does (MSWindowsScreen::forceShowCursor): it forces MouseKeys on, keeps the
/// user's own settings, and puts them back when the cursor leaves. Dropping it also
/// restores. While the cursor is here it also turns off Windows' "hide pointer while
/// typing" (MouseVanish): keys forwarded from the other computer would hide the
/// pointer until a real mouse move. Does nothing while a mouse is present. Off
/// Windows it does nothing.
#[cfg(windows)]
#[derive(Default)]
pub struct CursorKeeper {
    /// The cursor is on this computer (set by the host's Enter/Leave).
    on_screen: bool,
    /// The user's settings, held while we have them forced.
    saved: Option<Saved>,
    /// A refusal was already reported; avoids a log line on every tick.
    warned: bool,
    /// When the pointer was last woken with a nudge (rate limit).
    last_wake: Option<std::time::Instant>,
    /// When the last "put back" note was logged (rate limit).
    last_note: Option<std::time::Instant>,
}

/// What `CursorKeeper` changed, and the user's values from before.
#[cfg(windows)]
#[derive(Clone, Copy)]
struct Saved {
    keys: mouse_keys::MouseKeys,
    /// The user's hide-pointer-while-typing setting; None if Windows would not say.
    vanish: Option<bool>,
}

#[cfg(windows)]
impl CursorKeeper {
    pub fn new() -> Self {
        Self::default()
    }

    /// True while this keeper has MouseKeys forced on.
    pub fn is_active(&self) -> bool {
        self.saved.is_some()
    }

    /// The cursor entered (`true`) or left (`false`) this computer. Idempotent.
    pub fn set_active(&mut self, on_screen: bool) -> Option<CursorChange> {
        self.on_screen = on_screen;
        if !on_screen {
            self.warned = false;
        }
        self.sync()
    }

    /// Called every `CURSOR_CHECK` while the cursor is here: another app may switch
    /// MouseKeys or hide-while-typing back on, and a mouse may be plugged in or
    /// unplugged.
    pub fn tick(&mut self) -> Option<CursorChange> {
        self.sync()
    }

    fn sync(&mut self) -> Option<CursorChange> {
        let wanted = self.on_screen && !mouse_keys::mouse_present();
        let now = std::time::Instant::now();
        match (self.is_active(), wanted) {
            (false, true) => self.enable(),
            (true, false) => self.restore(),
            (true, true) => {
                let change = self.reassert(now);
                self.wake(now);
                change
            }
            (false, false) => None,
        }
    }

    fn enable(&mut self) -> Option<CursorChange> {
        let Some(cur) = mouse_keys::get() else {
            return self.refused();
        };
        if !mouse_keys::force_on(cur) {
            return self.refused();
        }
        let vanish = mouse_vanish::get();
        // A refusal here is retried by `reassert` on the next tick.
        if vanish == Some(true) {
            mouse_vanish::set(false);
        }
        self.saved = Some(Saved { keys: cur, vanish });
        self.warned = false;
        Some(CursorChange::Enabled)
    }

    fn restore(&mut self) -> Option<CursorChange> {
        let saved = self.saved?;
        if put_back(saved) {
            self.saved = None;
            self.warned = false;
            Some(CursorChange::Restored)
        } else {
            // Keep the saved settings so the next tick tries again.
            self.refused()
        }
    }

    /// Puts MouseKeys on and hide-while-typing off again if Windows or another program
    /// switched them back. Reads first (cheap); a write is made only if a value differs.
    fn reassert(&mut self, now: std::time::Instant) -> Option<CursorChange> {
        let mut changed = false;
        let mut failed = false;
        if let Some(cur) = mouse_keys::get() {
            if !mouse_keys::is_on(&cur) {
                changed = true;
                failed |= !mouse_keys::force_on(cur);
            }
        }
        if mouse_vanish::get() == Some(true) {
            changed = true;
            failed |= !mouse_vanish::set(false);
        }
        if failed {
            return self.refused();
        }
        self.warned = false;
        if changed && due(self.last_note, now, NOTE_EVERY) {
            self.last_note = Some(now);
            return Some(CursorChange::Reasserted);
        }
        None
    }

    /// If Windows still reports the pointer hidden (hide-while-typing or a stale state),
    /// nudge it: a 1 px move and back, at most once a second. `SetCursorPos` alone does
    /// not clear a hidden pointer.
    fn wake(&mut self, now: std::time::Instant) {
        if cursor_visibility() == Some(false) && due(self.last_wake, now, WAKE_EVERY) {
            self.last_wake = Some(now);
            nudge();
        }
    }

    fn refused(&mut self) -> Option<CursorChange> {
        if self.warned {
            None
        } else {
            self.warned = true;
            Some(CursorChange::Failed)
        }
    }
}

#[cfg(windows)]
impl Drop for CursorKeeper {
    fn drop(&mut self) {
        if let Some(saved) = self.saved.take() {
            put_back(saved);
        }
    }
}

/// Writes the user's settings back. True only if every write was accepted.
#[cfg(windows)]
fn put_back(saved: Saved) -> bool {
    let mut keys = saved.keys;
    let keys_ok = mouse_keys::set(&mut keys);
    let vanish_ok = saved.vanish.is_none_or(mouse_vanish::set);
    keys_ok && vanish_ok
}

/// Windows MouseKeys. winapi 0.3 has SPI_GET/SETMOUSEKEYS but neither the MOUSEKEYS
/// struct nor its MKF_ flags, so they are defined here (layout and values from the
/// Microsoft documentation of MOUSEKEYS).
#[cfg(windows)]
mod mouse_keys {
    use std::mem::{size_of, zeroed};
    use winapi::{
        ctypes::c_void,
        shared::minwindef::{DWORD, FALSE, UINT},
        um::winuser::{
            GetKeyState, GetSystemMetrics, SystemParametersInfoW, SM_MOUSEPRESENT, SPIF_SENDCHANGE,
            SPI_GETMOUSEKEYS, SPI_SETMOUSEKEYS, VK_NUMLOCK,
        },
    };

    #[repr(C)]
    #[derive(Clone, Copy)]
    #[allow(non_snake_case)]
    pub struct MouseKeys {
        pub cbSize: UINT,
        pub dwFlags: DWORD,
        pub iMaxSpeed: DWORD,
        pub iTimeToMaxSpeed: DWORD,
        pub iCtrlSpeed: DWORD,
        pub dwReserved1: DWORD,
        pub dwReserved2: DWORD,
    }

    const MKF_MOUSEKEYSON: DWORD = 0x0000_0001;
    const MKF_AVAILABLE: DWORD = 0x0000_0002;
    const MKF_REPLACENUMBERS: DWORD = 0x0000_0080;

    /// Does Windows see a mouse? Without one it draws no pointer.
    pub fn mouse_present() -> bool {
        // SAFETY: plain value query.
        unsafe { GetSystemMetrics(SM_MOUSEPRESENT) != 0 }
    }

    /// The current MouseKeys settings, or None if Windows refuses the query.
    pub fn get() -> Option<MouseKeys> {
        // SAFETY: an all-zero MouseKeys is a valid blank.
        let mut mk: MouseKeys = unsafe { zeroed() };
        let size = size_of::<MouseKeys>() as UINT;
        mk.cbSize = size;
        // SAFETY: `mk` is a valid out-structure; uiParam is its size, as the API requires.
        let ok = unsafe {
            SystemParametersInfoW(
                SPI_GETMOUSEKEYS,
                size,
                &mut mk as *mut MouseKeys as *mut c_void,
                0,
            )
        };
        (ok != FALSE).then_some(mk)
    }

    /// Writes `mk` system-wide and tells other windows (SPIF_SENDCHANGE, as Input Leap
    /// does). Not SPIF_UPDATEINIFILE: the change is temporary.
    pub fn set(mk: &mut MouseKeys) -> bool {
        let size = size_of::<MouseKeys>() as UINT;
        mk.cbSize = size;
        // SAFETY: `mk` is a valid in-structure; uiParam is its size, as the API requires.
        unsafe {
            SystemParametersInfoW(
                SPI_SETMOUSEKEYS,
                size,
                mk as *mut MouseKeys as *mut c_void,
                SPIF_SENDCHANGE,
            ) != FALSE
        }
    }

    /// `cur` with MouseKeys forced on. NumLock on makes the number keys replace the
    /// numpad keys, as in Input Leap. MKF_HOTKEYACTIVE is deliberately left off.
    pub fn force_on(mut cur: MouseKeys) -> bool {
        // SAFETY: plain state query; the low bit is the NumLock toggle state.
        let numlock = (unsafe { GetKeyState(VK_NUMLOCK) } & 1) != 0;
        let replace = if numlock { MKF_REPLACENUMBERS } else { 0 };
        cur.dwFlags = MKF_AVAILABLE | MKF_MOUSEKEYSON | replace;
        set(&mut cur)
    }

    /// True if MouseKeys is switched on in `mk`.
    pub fn is_on(mk: &MouseKeys) -> bool {
        mk.dwFlags & MKF_MOUSEKEYSON != 0
    }
}

/// Windows "hide pointer while typing" (MouseVanish). Per Microsoft's docs,
/// SPI_GETMOUSEVANISH writes a BOOL through pvParam, and SPI_SETMOUSEVANISH takes
/// TRUE/FALSE as the pvParam value itself (not a pointer).
#[cfg(windows)]
mod mouse_vanish {
    use winapi::{
        ctypes::c_void,
        shared::minwindef::{BOOL, FALSE, TRUE},
        um::winuser::{
            SystemParametersInfoW, SPIF_SENDCHANGE, SPI_GETMOUSEVANISH, SPI_SETMOUSEVANISH,
        },
    };

    /// Is hide-pointer-while-typing on? None if Windows refuses the query.
    pub fn get() -> Option<bool> {
        let mut on: BOOL = FALSE;
        // SAFETY: `on` is a valid BOOL out-parameter for SPI_GETMOUSEVANISH; uiParam is 0.
        let ok = unsafe {
            SystemParametersInfoW(
                SPI_GETMOUSEVANISH,
                0,
                &mut on as *mut BOOL as *mut c_void,
                0,
            )
        };
        (ok != FALSE).then_some(on != FALSE)
    }

    /// Switches hide-pointer-while-typing on or off until the next change (not saved
    /// to the profile), and tells other windows.
    pub fn set(on: bool) -> bool {
        let value: BOOL = if on { TRUE } else { FALSE };
        // SAFETY: for SPI_SETMOUSEVANISH pvParam carries the TRUE/FALSE value itself;
        // uiParam is unused and 0.
        unsafe {
            SystemParametersInfoW(
                SPI_SETMOUSEVANISH,
                0,
                value as usize as *mut c_void,
                SPIF_SENDCHANGE,
            ) != FALSE
        }
    }
}

/// Wakes a hidden pointer with a 1 px relative move and back (SendInput). Used when
/// Windows reports the pointer hidden; a SetCursorPos does not clear that state.
#[cfg(windows)]
fn nudge() -> bool {
    use std::mem::{size_of, zeroed};
    use winapi::{
        ctypes::c_int,
        shared::minwindef::UINT,
        um::winuser::{SendInput, INPUT, INPUT_MOUSE, MOUSEEVENTF_MOVE, MOUSEINPUT},
    };
    fn step(dx: i32) -> INPUT {
        // SAFETY: an all-zero INPUT is a valid blank value.
        let mut input: INPUT = unsafe { zeroed() };
        input.type_ = INPUT_MOUSE;
        // SAFETY: the union member written is the mouse one, matching type_.
        unsafe {
            *input.u.mi_mut() = MOUSEINPUT {
                dx,
                dy: 0,
                mouseData: 0,
                dwFlags: MOUSEEVENTF_MOVE,
                time: 0,
                dwExtraInfo: 0,
            };
        }
        input
    }
    let mut inputs = [step(1), step(-1)];
    // SAFETY: `inputs` holds two valid INPUT values; cbSize is size_of::<INPUT>().
    unsafe {
        SendInput(
            inputs.len() as UINT,
            inputs.as_mut_ptr(),
            size_of::<INPUT>() as c_int,
        ) == 2
    }
}

/// How often the guest checks MouseKeys and hide-while-typing while the cursor is on
/// this computer.
pub const CURSOR_CHECK: Duration = Duration::from_millis(500);
/// Smallest gap between two pointer wake-up nudges.
#[cfg(windows)]
const WAKE_EVERY: Duration = Duration::from_secs(1);
/// Smallest gap between two "put back" notes, so a program that keeps switching the
/// setting does not flood the log.
#[cfg(windows)]
const NOTE_EVERY: Duration = Duration::from_secs(10);

/// True when `last` is unset or at least `every` before `now`.
fn due(last: Option<std::time::Instant>, now: std::time::Instant, every: Duration) -> bool {
    match last {
        None => true,
        Some(t) => now.saturating_duration_since(t) >= every,
    }
}

pub const INPUT_PAUSED: &str = "Windows is showing a protected screen (UAC/lock): input paused";
pub const INPUT_RESUMED: &str = "Input resumed";

/// Failed injections in a row before the guest counts its input as paused.
const PAUSE_AFTER: u32 = 3;

/// Watches whether injected input gets through. Injection from this process fails
/// while Windows shows the secure desktop (UAC consent, lock screen, Ctrl+Alt+Del).
/// Pure state: no OS calls, so it is tested on every OS.
#[derive(Default)]
pub struct InjectHealth {
    streak: u32,
    paused: bool,
}

impl InjectHealth {
    /// Records one injection result. Returns the note to log when the state changes:
    /// once when input pauses, once when it resumes, nothing in between.
    pub fn record(&mut self, ok: bool) -> Option<&'static str> {
        if ok {
            self.streak = 0;
            return std::mem::take(&mut self.paused).then_some(INPUT_RESUMED);
        }
        self.streak = self.streak.saturating_add(1);
        if !self.paused && self.streak >= PAUSE_AFTER {
            self.paused = true;
            return Some(INPUT_PAUSED);
        }
        None
    }
}

/// Off Windows nothing is needed: only Windows draws no pointer without a mouse.
#[cfg(not(windows))]
#[derive(Default)]
pub struct CursorKeeper;

#[cfg(not(windows))]
impl CursorKeeper {
    pub fn new() -> Self {
        Self
    }

    pub fn is_active(&self) -> bool {
        false
    }

    pub fn set_active(&mut self, _on_screen: bool) -> Option<CursorChange> {
        None
    }

    pub fn tick(&mut self) -> Option<CursorChange> {
        None
    }
}

pub type SharedEngine = Arc<Mutex<HostEngine>>;

fn lock(e: &SharedEngine) -> std::sync::MutexGuard<'_, HostEngine> {
    e.lock().unwrap_or_else(|p| p.into_inner())
}

/// Replace the engine's layout (new guest connected).
pub fn set_engine(e: &SharedEngine, new: HostEngine) {
    *lock(e) = new;
}

/// Take control back locally (guest gone). Sends nothing: the link is dead.
/// The warp back is logged like any other (see `WarpLog`).
pub fn release_host(e: &SharedEngine, log: impl Fn(String)) {
    if let Some((x, y)) = lock(e).force_local().warp {
        log(warp_line((x, y), &warp(x, y, true)));
    }
}

/// Blocks the calling thread, grabbing all input for the life of the process.
/// With no links in the engine every event passes through untouched.
/// Needs Accessibility + Input Monitoring permission on macOS.
/// `log` receives the engine's diagnostic lines (see `Step::trace`) and the
/// warp results (see `WarpLog`).
#[cfg(any(target_os = "macos", target_os = "windows"))]
pub fn run_host(
    engine: SharedEngine,
    tx: UnboundedSender<(PeerId, Msg)>,
    log: impl Fn(String) + Send + 'static,
) -> Result<()> {
    let warps = Mutex::new(WarpLog::default());
    rdev::grab(move |event| {
        let Some(ev) = convert(&event.event_type) else {
            return Some(event);
        };
        let step = lock(&engine).on_event(ev);
        let note = {
            let mut w = warps.lock().unwrap_or_else(|p| p.into_inner());
            apply(&step, &tx, &mut w)
        };
        if let Some(line) = &step.trace {
            log(line.clone());
        }
        if let Some(line) = note {
            log(line);
        }
        if step.swallow {
            None
        } else {
            Some(event)
        }
    })
    .map_err(|e| anyhow!("input grab failed: {e:?} (macOS: grant Accessibility and Input Monitoring to this app)"))
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn run_host(
    _: SharedEngine,
    _: UnboundedSender<(PeerId, Msg)>,
    _: impl Fn(String) + Send + 'static,
) -> Result<()> {
    Err(anyhow!(
        "host mode is not supported on this OS yet (Windows and macOS only)"
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use mono_proto::Key;

    #[cfg(not(windows))]
    #[test]
    fn cursor_keeper_does_nothing_off_windows() {
        let mut k = CursorKeeper::new();
        assert_eq!(k.set_active(true), None);
        assert_eq!(k.tick(), None);
        assert!(!k.is_active());
        assert_eq!(k.set_active(false), None);
    }

    #[test]
    fn warp_log_traces_every_warp_after_a_crossing() {
        let mut w = WarpLog::default();
        let now = Instant::now();
        let target = (960.0, 540.0);
        let landed = Ok((960.0, 540.0));
        for _ in 0..WARP_TRACE {
            assert_eq!(
                w.note(target, &landed, now).as_deref(),
                Some("warp to (960,540) -> OS at (960,540)")
            );
        }
        assert_eq!(w.note(target, &landed, now), None);
        w.crossed();
        assert!(w.note(target, &landed, now).is_some());
    }

    /// Only the trace window reads the landing back; later warps skip the readback.
    #[test]
    fn warp_readback_is_wanted_only_in_the_trace_window() {
        let mut w = WarpLog::default();
        let now = Instant::now();
        let landed = Ok((960.0, 540.0));
        for _ in 0..WARP_TRACE {
            assert!(w.tracing());
            w.note((960.0, 540.0), &landed, now);
        }
        assert!(!w.tracing(), "per-event warps still read back");
        w.crossed();
        assert!(w.tracing());
    }

    #[test]
    fn warp_log_throttles_failures_once_the_trace_is_over() {
        let mut w = WarpLog::default();
        let t0 = Instant::now();
        let target = (960.0, 540.0);
        let missed = Err("OS at (959,540), not (960,540)".to_string());
        for _ in 0..WARP_TRACE {
            assert!(w.note(target, &missed, t0).is_some());
        }
        assert_eq!(w.note(target, &missed, t0), None);
        assert_eq!(w.note(target, &missed, t0 + Duration::from_secs(1)), None);
        assert_eq!(
            w.note(target, &missed, t0 + WARP_FAIL_EVERY),
            Some("warp failed: OS at (959,540), not (960,540)".to_string())
        );
    }

    #[test]
    fn warp_log_is_silent_about_landed_warps_after_the_trace() {
        let mut w = WarpLog::default();
        let now = Instant::now();
        let landed = Ok((960.0, 540.0));
        for _ in 0..WARP_TRACE {
            w.note((960.0, 540.0), &landed, now);
        }
        for _ in 0..50 {
            assert_eq!(w.note((960.0, 540.0), &landed, now), None);
        }
    }

    #[test]
    fn due_respects_the_interval() {
        let t0 = std::time::Instant::now();
        let every = Duration::from_secs(1);
        assert!(due(None, t0, every));
        assert!(!due(Some(t0), t0 + Duration::from_millis(999), every));
        assert!(due(Some(t0), t0 + every, every));
    }

    #[test]
    fn inject_health_pauses_once_and_resumes_once() {
        let mut h = InjectHealth::default();
        assert_eq!(h.record(false), None);
        assert_eq!(h.record(false), None);
        assert_eq!(h.record(false), Some(INPUT_PAUSED));
        for _ in 0..50 {
            assert_eq!(h.record(false), None);
        }
        assert_eq!(h.record(true), Some(INPUT_RESUMED));
        assert_eq!(h.record(true), None);
    }

    #[test]
    fn inject_health_ignores_failures_that_are_not_a_run() {
        let mut h = InjectHealth::default();
        for _ in 0..10 {
            assert_eq!(h.record(false), None);
            assert_eq!(h.record(false), None);
            assert_eq!(h.record(true), None);
        }
    }

    #[test]
    fn inject_health_pauses_again_after_a_resume() {
        let mut h = InjectHealth::default();
        for _ in 0..3 {
            h.record(false);
        }
        assert_eq!(h.record(true), Some(INPUT_RESUMED));
        assert_eq!(h.record(false), None);
        assert_eq!(h.record(false), None);
        assert_eq!(h.record(false), Some(INPUT_PAUSED));
    }

    #[test]
    fn inject_health_success_before_pause_stays_silent() {
        let mut h = InjectHealth::default();
        assert_eq!(h.record(true), None);
        assert_eq!(h.record(false), None);
        assert_eq!(h.record(true), None);
    }

    #[test]
    fn convert_maps_all_event_kinds() {
        assert_eq!(
            convert(&EventType::KeyPress(Key::KeyA)),
            Some(Ev::Key {
                key: Key::KeyA,
                down: true
            })
        );
        assert_eq!(
            convert(&EventType::MouseMove { x: 1.0, y: 2.0 }),
            Some(Ev::Move { x: 1.0, y: 2.0 })
        );
        assert_eq!(
            convert(&EventType::Wheel {
                delta_x: 1,
                delta_y: -2
            }),
            Some(Ev::Wheel { dx: 1, dy: -2 })
        );
    }
}
