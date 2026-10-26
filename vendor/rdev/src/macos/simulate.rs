use crate::rdev::{Button, EventType, SimulateError};
use core_graphics::event::{
    CGEvent, CGEventTapLocation, CGEventType, CGMouseButton, EventField, ScrollEventUnit,
};
use core_graphics::event_source::{CGEventSource, CGEventSourceStateID};
use core_graphics::geometry::CGPoint;
use std::convert::TryInto;
use std::sync::Mutex;

use crate::macos::keycodes::code_from_key;

/// Buttons held down by simulated presses, oldest first. While one is held a
/// move must be posted as a Dragged event: macOS does not drag windows or
/// extend selections on a plain MouseMoved.
static HELD: Mutex<Vec<Button>> = Mutex::new(Vec::new());

/// Phase of a button event. `Drag` is a move while a button is held.
#[derive(Clone, Copy)]
enum Phase {
    Down,
    Up,
    Drag,
}

/// CoreGraphics event type for `phase` of `button`.
fn event_type_for(phase: Phase, button: Button) -> CGEventType {
    match (button, phase) {
        (Button::Left, Phase::Down) => CGEventType::LeftMouseDown,
        (Button::Left, Phase::Up) => CGEventType::LeftMouseUp,
        (Button::Left, Phase::Drag) => CGEventType::LeftMouseDragged,
        (Button::Right, Phase::Down) => CGEventType::RightMouseDown,
        (Button::Right, Phase::Up) => CGEventType::RightMouseUp,
        (Button::Right, Phase::Drag) => CGEventType::RightMouseDragged,
        (_, Phase::Down) => CGEventType::OtherMouseDown,
        (_, Phase::Up) => CGEventType::OtherMouseUp,
        (_, Phase::Drag) => CGEventType::OtherMouseDragged,
    }
}

/// Button number macOS uses for `button`: 0 left, 1 right, 2 middle.
fn button_number(button: Button) -> i64 {
    match button {
        Button::Left => 0,
        Button::Right => 1,
        Button::Middle => 2,
        Button::Unknown(n) => i64::from(n),
    }
}

/// Mouse event for `phase` of `button` at `point`.
fn button_event(
    source: CGEventSource,
    phase: Phase,
    point: CGPoint,
    button: Button,
) -> Option<CGEvent> {
    let cg_button = match button {
        Button::Left => CGMouseButton::Left,
        Button::Right => CGMouseButton::Right,
        Button::Middle | Button::Unknown(_) => CGMouseButton::Center,
    };
    let event =
        CGEvent::new_mouse_event(source, event_type_for(phase, button), point, cg_button).ok()?;
    // Other buttons are identified by this field, not by the event type.
    event.set_integer_value_field(EventField::MOUSE_EVENT_BUTTON_NUMBER, button_number(button));
    Some(event)
}

/// The button a move drags with: the oldest one still held.
fn held_button() -> Option<Button> {
    HELD.lock().ok().and_then(|held| held.first().copied())
}

/// Keeps `held` in step with a simulated button press or release.
fn track_button(held: &mut Vec<Button>, event_type: &EventType) {
    match event_type {
        EventType::ButtonPress(button) => {
            if !held.contains(button) {
                held.push(*button);
            }
        }
        EventType::ButtonRelease(button) => held.retain(|b| b != button),
        _ => {}
    }
}

unsafe fn convert_native_with_source(
    event_type: &EventType,
    source: CGEventSource,
) -> Option<CGEvent> {
    match event_type {
        EventType::KeyPress(key) => {
            let code = code_from_key(*key)?;
            CGEvent::new_keyboard_event(source, code, true).ok()
        }
        EventType::KeyRelease(key) => {
            let code = code_from_key(*key)?;
            CGEvent::new_keyboard_event(source, code, false).ok()
        }
        EventType::ButtonPress(button) => {
            let point = get_current_mouse_location()?;
            button_event(source, Phase::Down, point, *button)
        }
        EventType::ButtonRelease(button) => {
            let point = get_current_mouse_location()?;
            button_event(source, Phase::Up, point, *button)
        }
        EventType::MouseMove { x, y } => {
            let point = CGPoint { x: (*x), y: (*y) };
            match held_button() {
                Some(button) => button_event(source, Phase::Drag, point, button),
                None => CGEvent::new_mouse_event(
                    source,
                    CGEventType::MouseMoved,
                    point,
                    CGMouseButton::Left,
                )
                .ok(),
            }
        }
        EventType::Wheel { delta_x, delta_y } => {
            let wheel_count = 2;
            CGEvent::new_scroll_event(
                source,
                ScrollEventUnit::PIXEL,
                wheel_count,
                (*delta_y).try_into().ok()?,
                (*delta_x).try_into().ok()?,
                0,
            )
            .ok()
        }
    }
}

unsafe fn convert_native(event_type: &EventType) -> Option<CGEvent> {
    let source = CGEventSource::new(CGEventSourceStateID::HIDSystemState).ok()?;
    convert_native_with_source(event_type, source)
}

unsafe fn get_current_mouse_location() -> Option<CGPoint> {
    let source = CGEventSource::new(CGEventSourceStateID::HIDSystemState).ok()?;
    let event = CGEvent::new(source).ok()?;
    Some(event.location())
}

#[link(name = "Cocoa", kind = "framework")]
extern "C" {}

pub fn simulate(event_type: &EventType) -> Result<(), SimulateError> {
    unsafe {
        if let Some(cg_event) = convert_native(event_type) {
            if let Ok(mut held) = HELD.lock() {
                track_button(&mut held, event_type);
            }
            cg_event.post(CGEventTapLocation::HID);
            Ok(())
        } else {
            Err(SimulateError)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn held_buttons_are_tracked_in_press_order() {
        let mut held = Vec::new();
        track_button(&mut held, &EventType::ButtonPress(Button::Middle));
        track_button(&mut held, &EventType::ButtonPress(Button::Left));
        track_button(&mut held, &EventType::ButtonPress(Button::Left));
        assert_eq!(held, vec![Button::Middle, Button::Left]);
        track_button(&mut held, &EventType::ButtonRelease(Button::Middle));
        assert_eq!(held, vec![Button::Left]);
        track_button(&mut held, &EventType::ButtonRelease(Button::Right));
        assert_eq!(held, vec![Button::Left]);
    }

    #[test]
    fn other_buttons_use_other_mouse_events() {
        assert_eq!(
            event_type_for(Phase::Down, Button::Middle) as u32,
            CGEventType::OtherMouseDown as u32
        );
        assert_eq!(
            event_type_for(Phase::Up, Button::Unknown(4)) as u32,
            CGEventType::OtherMouseUp as u32
        );
        assert_eq!(
            event_type_for(Phase::Drag, Button::Middle) as u32,
            CGEventType::OtherMouseDragged as u32
        );
        assert_eq!(
            event_type_for(Phase::Drag, Button::Left) as u32,
            CGEventType::LeftMouseDragged as u32
        );
        assert_eq!(
            event_type_for(Phase::Drag, Button::Right) as u32,
            CGEventType::RightMouseDragged as u32
        );
        assert_eq!(button_number(Button::Left), 0);
        assert_eq!(button_number(Button::Right), 1);
        assert_eq!(button_number(Button::Middle), 2);
        assert_eq!(button_number(Button::Unknown(4)), 4);
    }
}
