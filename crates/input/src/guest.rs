//! Guest side: turns protocol messages into injection actions and guarantees
//! nothing stays held when control leaves or the link dies (FR-6).
use crate::guard::PressedGuard;
use crate::layout::Size;
use mono_proto::{Button, Key, Msg};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Act {
    MoveTo(f64, f64),
    Key { key: Key, down: bool },
    Button { button: Button, down: bool },
    Wheel { dx: i64, dy: i64 },
}

pub struct GuestEngine {
    screen: Size,
    pos: (f64, f64),
    guard: PressedGuard,
    active: bool,
}

impl GuestEngine {
    pub fn new(screen: Size) -> Self {
        Self {
            screen,
            pos: (0.0, 0.0),
            guard: PressedGuard::default(),
            active: false,
        }
    }

    /// Input is ignored until the host has sent `Enter`.
    pub fn on_msg(&mut self, m: &Msg) -> Vec<Act> {
        match *m {
            Msg::Enter { x, y } => {
                self.active = true;
                self.pos = (
                    f64::from(x.clamp(0.0, 1.0)) * (self.screen.w - 1.0),
                    f64::from(y.clamp(0.0, 1.0)) * (self.screen.h - 1.0),
                );
                vec![Act::MoveTo(self.pos.0, self.pos.1)]
            }
            Msg::Leave => self.release(),
            _ if !self.active => vec![],
            Msg::Move { dx, dy } => {
                self.pos.0 = (self.pos.0 + f64::from(dx)).clamp(0.0, self.screen.w - 1.0);
                self.pos.1 = (self.pos.1 + f64::from(dy)).clamp(0.0, self.screen.h - 1.0);
                vec![Act::MoveTo(self.pos.0, self.pos.1)]
            }
            Msg::Key { key, down } => {
                self.guard.observe(m);
                vec![Act::Key { key, down }]
            }
            Msg::Button { button, down } => {
                self.guard.observe(m);
                vec![Act::Button { button, down }]
            }
            Msg::Wheel { dx, dy } => vec![Act::Wheel { dx, dy }],
            Msg::Hello { .. } | Msg::HelloOk | Msg::Screen { .. } | Msg::Clip { .. } => vec![],
        }
    }

    /// Call when the link drops.
    pub fn on_disconnect(&mut self) -> Vec<Act> {
        self.release()
    }

    fn release(&mut self) -> Vec<Act> {
        self.active = false;
        self.guard
            .release_all()
            .into_iter()
            .filter_map(|m| match m {
                Msg::Key { key, down } => Some(Act::Key { key, down }),
                Msg::Button { button, down } => Some(Act::Button { button, down }),
                _ => None,
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn g() -> GuestEngine {
        GuestEngine::new(Size {
            w: 1000.0,
            h: 500.0,
        })
    }

    #[test]
    fn ignores_input_before_enter() {
        let mut e = g();
        assert!(e
            .on_msg(&Msg::Key {
                key: Key::KeyA,
                down: true
            })
            .is_empty());
        assert!(e.on_msg(&Msg::Move { dx: 5, dy: 5 }).is_empty());
    }

    #[test]
    fn enter_places_cursor_proportionally() {
        let mut e = g();
        assert_eq!(
            e.on_msg(&Msg::Enter { x: 0.0, y: 0.5 }),
            vec![Act::MoveTo(0.0, 249.5)]
        );
    }

    #[test]
    fn move_accumulates_and_clamps() {
        let mut e = g();
        e.on_msg(&Msg::Enter { x: 0.0, y: 0.0 });
        assert_eq!(
            e.on_msg(&Msg::Move { dx: 10, dy: 20 }),
            vec![Act::MoveTo(10.0, 20.0)]
        );
        assert_eq!(
            e.on_msg(&Msg::Move { dx: -100, dy: 9999 }),
            vec![Act::MoveTo(0.0, 499.0)]
        );
    }

    #[test]
    fn opposite_moves_keep_the_clamped_edge_path() {
        let mut e = g();
        e.on_msg(&Msg::Enter { x: 0.0, y: 0.5 });
        assert_eq!(
            e.on_msg(&Msg::Move { dx: -5, dy: 0 }),
            vec![Act::MoveTo(0.0, 249.5)]
        );
        assert_eq!(
            e.on_msg(&Msg::Move { dx: 5, dy: 0 }),
            vec![Act::MoveTo(5.0, 249.5)]
        );
    }

    #[test]
    fn leave_releases_held_keys_and_buttons() {
        let mut e = g();
        e.on_msg(&Msg::Enter { x: 0.0, y: 0.0 });
        e.on_msg(&Msg::Key {
            key: Key::ShiftLeft,
            down: true,
        });
        e.on_msg(&Msg::Button {
            button: Button::Left,
            down: true,
        });
        let mut acts = e.on_msg(&Msg::Leave);
        acts.sort_by_key(|a| format!("{a:?}"));
        assert_eq!(
            acts,
            vec![
                Act::Button {
                    button: Button::Left,
                    down: false
                },
                Act::Key {
                    key: Key::ShiftLeft,
                    down: false
                },
            ]
        );
        // inactive again
        assert!(e
            .on_msg(&Msg::Key {
                key: Key::KeyA,
                down: true
            })
            .is_empty());
    }

    #[test]
    fn disconnect_releases_too() {
        let mut e = g();
        e.on_msg(&Msg::Enter { x: 0.5, y: 0.5 });
        e.on_msg(&Msg::Key {
            key: Key::KeyA,
            down: true,
        });
        assert_eq!(
            e.on_disconnect(),
            vec![Act::Key {
                key: Key::KeyA,
                down: false
            }]
        );
        assert!(e.on_disconnect().is_empty());
    }
}
