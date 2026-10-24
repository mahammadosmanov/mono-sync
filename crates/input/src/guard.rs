//! Tracks keys/buttons the guest believes are held, so a disconnect can
//! release them all (FR-6, threat T6).
use mono_proto::{Button, Key, Msg};
use std::collections::HashSet;

#[derive(Default)]
pub struct PressedGuard {
    keys: HashSet<Key>,
    buttons: HashSet<Button>,
}

impl PressedGuard {
    /// Call for every message about to be injected.
    pub fn observe(&mut self, m: &Msg) {
        match *m {
            Msg::Key { key, down: true } => {
                self.keys.insert(key);
            }
            Msg::Key { key, down: false } => {
                self.keys.remove(&key);
            }
            Msg::Button { button, down: true } => {
                self.buttons.insert(button);
            }
            Msg::Button {
                button,
                down: false,
            } => {
                self.buttons.remove(&button);
            }
            _ => {}
        }
    }

    /// Messages that release everything still held; clears the state.
    pub fn release_all(&mut self) -> Vec<Msg> {
        let mut out: Vec<Msg> = self
            .keys
            .drain()
            .map(|key| Msg::Key { key, down: false })
            .collect();
        out.extend(self.buttons.drain().map(|button| Msg::Button {
            button,
            down: false,
        }));
        out
    }

    pub fn is_idle(&self) -> bool {
        self.keys.is_empty() && self.buttons.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn releases_only_what_is_held() {
        let mut g = PressedGuard::default();
        g.observe(&Msg::Key {
            key: Key::KeyA,
            down: true,
        });
        g.observe(&Msg::Key {
            key: Key::KeyB,
            down: true,
        });
        g.observe(&Msg::Key {
            key: Key::KeyB,
            down: false,
        });
        g.observe(&Msg::Button {
            button: Button::Left,
            down: true,
        });
        let mut r = g.release_all();
        r.sort_by_key(|m| format!("{m:?}"));
        assert_eq!(
            r,
            vec![
                Msg::Button {
                    button: Button::Left,
                    down: false
                },
                Msg::Key {
                    key: Key::KeyA,
                    down: false
                },
            ]
        );
        assert!(g.is_idle());
        assert!(g.release_all().is_empty());
    }

    #[test]
    fn repeated_down_releases_once() {
        let mut g = PressedGuard::default();
        for _ in 0..3 {
            g.observe(&Msg::Key {
                key: Key::ShiftLeft,
                down: true,
            });
        }
        assert_eq!(g.release_all().len(), 1);
    }
}
