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
use tokio::sync::mpsc::UnboundedSender;

pub fn screen_size() -> Result<Size> {
    let (w, h) = rdev::display_size().map_err(|e| anyhow!("display size: {e:?}"))?;
    Ok(Size {
        w: w as f64,
        h: h as f64,
    })
}

fn simulate(e: &EventType) -> Result<()> {
    rdev::simulate(e).map_err(|_| anyhow!("input injection failed for {e:?}"))
}

pub fn warp(x: f64, y: f64) {
    let _ = simulate(&EventType::MouseMove { x, y });
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

fn apply(step: &Step, tx: &UnboundedSender<(PeerId, Msg)>) {
    for (p, m) in &step.send {
        let _ = tx.send((*p, m.clone()));
    }
    if let Some((x, y)) = step.warp {
        warp(x, y);
    }
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
}

/// Makes Windows draw the pointer while the cursor is on this computer and no mouse
/// is attached. Windows draws no pointer without a mouse, so this does what Input
/// Leap does (MSWindowsScreen::forceShowCursor): it forces MouseKeys on, keeps the
/// user's own settings, and puts them back when the cursor leaves. Dropping it also
/// restores. Does nothing while a mouse is present. Off Windows it does nothing.
#[cfg(windows)]
#[derive(Default)]
pub struct CursorKeeper {
    /// The cursor is on this computer (set by the host's Enter/Leave).
    on_screen: bool,
    /// The user's MouseKeys settings, held while we have MouseKeys forced on.
    saved: Option<mouse_keys::MouseKeys>,
    /// A refusal was already reported; avoids a log line on every tick.
    warned: bool,
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

    /// Called about every 2 s while the cursor is here: another app may switch
    /// MouseKeys off again, and a mouse may be plugged in or unplugged.
    pub fn tick(&mut self) -> Option<CursorChange> {
        self.sync()
    }

    fn sync(&mut self) -> Option<CursorChange> {
        let wanted = self.on_screen && !mouse_keys::mouse_present();
        match (self.is_active(), wanted) {
            (false, true) => self.enable(),
            (true, false) => self.restore(),
            (true, true) => {
                mouse_keys::reassert();
                None
            }
            (false, false) => None,
        }
    }

    fn enable(&mut self) -> Option<CursorChange> {
        if let Some(cur) = mouse_keys::get() {
            if mouse_keys::force_on(cur) {
                self.saved = Some(cur);
                self.warned = false;
                return Some(CursorChange::Enabled);
            }
        }
        self.refused()
    }

    fn restore(&mut self) -> Option<CursorChange> {
        let mut old = self.saved?;
        if mouse_keys::set(&mut old) {
            self.saved = None;
            self.warned = false;
            Some(CursorChange::Restored)
        } else {
            // Keep the saved settings so the next tick tries again.
            self.refused()
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
        if let Some(mut old) = self.saved.take() {
            mouse_keys::set(&mut old);
        }
    }
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

    /// Turns MouseKeys back on if something switched it off. Input Leap does this on
    /// each settings-change broadcast; we check on a timer instead.
    pub fn reassert() {
        if let Some(cur) = get() {
            if cur.dwFlags & MKF_MOUSEKEYSON == 0 {
                force_on(cur);
            }
        }
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
pub fn release_host(e: &SharedEngine) {
    if let Some((x, y)) = lock(e).force_local().warp {
        warp(x, y);
    }
}

/// Blocks the calling thread, grabbing all input for the life of the process.
/// With no links in the engine every event passes through untouched.
/// Needs Accessibility + Input Monitoring permission on macOS.
#[cfg(any(target_os = "macos", target_os = "windows"))]
pub fn run_host(engine: SharedEngine, tx: UnboundedSender<(PeerId, Msg)>) -> Result<()> {
    rdev::grab(move |event| {
        let Some(ev) = convert(&event.event_type) else {
            return Some(event);
        };
        let step = lock(&engine).on_event(ev);
        apply(&step, &tx);
        if step.swallow {
            None
        } else {
            Some(event)
        }
    })
    .map_err(|e| anyhow!("input grab failed: {e:?} (macOS: grant Accessibility and Input Monitoring to this app)"))
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn run_host(_: SharedEngine, _: UnboundedSender<(PeerId, Msg)>) -> Result<()> {
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
