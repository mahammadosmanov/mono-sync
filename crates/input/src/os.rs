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

/// Where the OS says the cursor is right now (Windows only; used for diagnostics).
pub fn cursor_pos() -> Option<(i32, i32)> {
    #[cfg(windows)]
    {
        use winapi::{shared::windef::POINT, um::winuser::GetCursorPos};
        let mut p = POINT { x: 0, y: 0 };
        // SAFETY: `p` is a valid out-pointer for the duration of the call.
        if unsafe { GetCursorPos(&mut p) } != 0 {
            return Some((p.x, p.y));
        }
    }
    None
}

/// Windows only: `SetCursorPos` moves the cursor but does not un-hide it when
/// Windows considers it hidden/suppressed (no mouse input seen, touch or
/// "hide while typing"). A real, tiny mouse input does. No-op elsewhere.
pub fn wake_cursor() {
    #[cfg(windows)]
    {
        use std::mem::{size_of, zeroed};
        use winapi::um::winuser::{SendInput, INPUT, INPUT_MOUSE, MOUSEEVENTF_MOVE};
        for dx in [1i32, -1] {
            // SAFETY: zeroed INPUT is a valid blank; fields set below; size is exact.
            unsafe {
                let mut i: INPUT = zeroed();
                i.type_ = INPUT_MOUSE;
                let mi = i.u.mi_mut();
                mi.dx = dx;
                mi.dy = 0;
                mi.dwFlags = MOUSEEVENTF_MOVE;
                SendInput(1, &mut i, size_of::<INPUT>() as i32);
            }
        }
    }
}

/// Windows only: if Windows reports the cursor as hidden, nudge it back.
pub fn ensure_cursor_visible() {
    #[cfg(windows)]
    if cursor_visibility().starts_with("HIDDEN") {
        wake_cursor();
    }
}

/// Windows only: is the cursor currently drawn? e.g. "showing" / "HIDDEN" / "suppressed".
pub fn cursor_visibility() -> String {
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
            let shown = ci.flags & CURSOR_SHOWING != 0;
            return format!(
                "{} (flags={}, handle={})",
                if shown { "showing" } else { "HIDDEN" },
                ci.flags,
                if ci.hCursor.is_null() { "none" } else { "set" }
            );
        }
    }
    String::new()
}

/// One line describing the monitors, for the log (Windows only).
pub fn display_info() -> String {
    #[cfg(windows)]
    {
        use winapi::um::winuser::*;
        // SAFETY: plain value queries.
        unsafe {
            return format!(
                "monitors={} primary={}x{} virtual={}x{} at ({},{})",
                GetSystemMetrics(SM_CMONITORS),
                GetSystemMetrics(SM_CXSCREEN),
                GetSystemMetrics(SM_CYSCREEN),
                GetSystemMetrics(SM_CXVIRTUALSCREEN),
                GetSystemMetrics(SM_CYVIRTUALSCREEN),
                GetSystemMetrics(SM_XVIRTUALSCREEN),
                GetSystemMetrics(SM_YVIRTUALSCREEN)
            );
        }
    }
    #[cfg(not(windows))]
    String::new()
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
