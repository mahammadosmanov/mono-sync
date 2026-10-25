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
