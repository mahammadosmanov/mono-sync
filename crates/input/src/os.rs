//! Thin OS glue over `rdev` (ADR-0005). No decisions are made here: events
//! are converted and handed to the pure engines.
// Host mode is stubbed on Linux (not supported yet), so helpers are unused there.
#![cfg_attr(not(any(target_os = "macos", target_os = "windows")), allow(dead_code, unused_imports))]

use crate::guest::Act;
use crate::host::{Ev, HostEngine, Step};
use crate::layout::{PeerId, Size};
use anyhow::{anyhow, Result};
use mono_proto::Msg;
use rdev::EventType;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
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

/// Blocks the calling thread, grabbing all input. Needs Accessibility +
/// Input Monitoring permission on macOS. When `alive` turns false, control
/// returns to the host on the next event.
#[cfg(any(target_os = "macos", target_os = "windows"))]
pub fn run_host(
    engine: HostEngine,
    tx: UnboundedSender<(PeerId, Msg)>,
    alive: Arc<AtomicBool>,
) -> Result<()> {
    use std::cell::RefCell;
    let engine = RefCell::new(engine);
    rdev::grab(move |event| {
        if !alive.load(Ordering::Relaxed) {
            apply(&engine.borrow_mut().force_local(), &tx);
        }
        let Some(ev) = convert(&event.event_type) else { return Some(event) };
        let step = engine.borrow_mut().on_event(ev);
        apply(&step, &tx);
        if step.swallow {
            None
        } else {
            Some(event)
        }
    })
    .map_err(|e| anyhow!("input grab failed: {e:?} (macOS: grant Accessibility and Input Monitoring to this terminal/app)"))
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
pub fn run_host(
    _: HostEngine,
    _: UnboundedSender<(PeerId, Msg)>,
    _: Arc<AtomicBool>,
) -> Result<()> {
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
