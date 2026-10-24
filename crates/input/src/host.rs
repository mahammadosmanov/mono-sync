//! Host side: decides, per physical event, whether to forward it to a peer,
//! swallow it locally and/or warp the local cursor.
//!
//! While a peer owns the cursor we keep the local cursor parked at the screen
//! centre and read motion as the offset from that centre (works with plain
//! absolute positions on every OS). The warp's own echo event is swallowed.
use crate::layout::{Link, Out, PeerId, Router, Size};
use mono_proto::{Button, Key, Msg};
use std::collections::HashSet;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Ev {
    Move { x: f64, y: f64 },
    Key { key: Key, down: bool },
    Button { button: Button, down: bool },
    Wheel { dx: i64, dy: i64 },
}

#[derive(Debug, Default, PartialEq)]
pub struct Step {
    pub send: Vec<(PeerId, Msg)>,
    /// Hide the event from the local OS.
    pub swallow: bool,
    /// Move the local cursor here after handling the event.
    pub warp: Option<(f64, f64)>,
}

pub struct HostEngine {
    host: Size,
    router: Router,
    last: (f64, f64),
    peer: Option<PeerId>,
    down: HashSet<Key>,
}

impl HostEngine {
    pub fn new(host: Size, links: Vec<Link>) -> Self {
        Self {
            host,
            router: Router::new(host, links),
            last: (host.w / 2.0, host.h / 2.0),
            peer: None,
            down: HashSet::new(),
        }
    }

    pub fn is_remote(&self) -> bool {
        self.peer.is_some()
    }

    fn center(&self) -> (f64, f64) {
        ((self.host.w / 2.0).floor(), (self.host.h / 2.0).floor())
    }

    pub fn on_event(&mut self, ev: Ev) -> Step {
        match ev {
            Ev::Move { x, y } => self.on_move(x, y),
            Ev::Key { key, down } => self.on_key(key, down),
            Ev::Button { button, down } => self.forward(Msg::Button { button, down }),
            Ev::Wheel { dx, dy } => self.forward(Msg::Wheel { dx, dy }),
        }
    }

    /// Link lost or user asked to come back: take control locally again.
    pub fn force_local(&mut self) -> Step {
        let Some(peer) = self.peer.take() else {
            return Step::default();
        };
        self.router.force_local();
        self.down.clear();
        Step {
            send: vec![(peer, Msg::Leave)],
            swallow: false,
            warp: Some(self.center()),
        }
    }

    fn forward(&mut self, m: Msg) -> Step {
        match self.peer {
            Some(p) => Step {
                send: vec![(p, m)],
                swallow: true,
                warp: None,
            },
            None => Step::default(),
        }
    }

    fn on_key(&mut self, key: Key, down: bool) -> Step {
        if self.peer.is_none() {
            return Step::default();
        }
        if down {
            self.down.insert(key);
        } else {
            self.down.remove(&key);
        }
        // Escape hatch: Ctrl + Alt/Option + Esc returns control to the host.
        let hotkey = down
            && key == Key::Escape
            && self.down.contains(&Key::ControlLeft)
            && self.down.contains(&Key::Alt);
        if hotkey {
            let mut s = self.force_local();
            s.swallow = true;
            return s;
        }
        self.forward(Msg::Key { key, down })
    }

    fn on_move(&mut self, x: f64, y: f64) -> Step {
        let Some(_) = self.peer else {
            let d = (x - self.last.0, y - self.last.1);
            self.last = (x, y);
            let outs = self.router.motion((x, y), d);
            return self.apply(outs);
        };
        let c = self.center();
        if (x - c.0).abs() < 1.0 && (y - c.1).abs() < 1.0 {
            return Step {
                swallow: true,
                ..Step::default()
            }; // echo of our own warp
        }
        let outs = self.router.motion((x, y), (x - c.0, y - c.1));
        let mut s = self.apply(outs);
        if self.peer.is_some() {
            s.warp = Some(c);
        }
        s
    }

    fn apply(&mut self, outs: Vec<Out>) -> Step {
        let mut s = Step::default();
        for o in outs {
            match o {
                Out::Enter { peer, x, y } => {
                    self.peer = Some(peer);
                    s.send.push((peer, Msg::Enter { x, y }));
                    s.warp = Some(self.center());
                }
                Out::Move { peer, dx, dy } => s.send.push((peer, Msg::Move { dx, dy })),
                Out::Leave {
                    peer,
                    host_x,
                    host_y,
                } => {
                    self.peer = None;
                    self.down.clear();
                    self.last = (host_x, host_y);
                    s.send.push((peer, Msg::Leave));
                    s.warp = Some((host_x, host_y));
                }
            }
        }
        s
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::layout::Edge;

    const HOST: Size = Size {
        w: 1920.0,
        h: 1080.0,
    };
    const GUEST: Size = Size {
        w: 1000.0,
        h: 500.0,
    };

    fn engine() -> HostEngine {
        HostEngine::new(
            HOST,
            vec![Link {
                edge: Edge::Right,
                peer: 1,
                size: GUEST,
            }],
        )
    }

    fn enter(e: &mut HostEngine) {
        e.on_event(Ev::Move {
            x: 1000.0,
            y: 540.0,
        });
        let s = e.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        });
        assert!(matches!(s.send[0].1, Msg::Enter { .. }));
        assert_eq!(s.warp, Some((960.0, 540.0)));
        assert!(e.is_remote());
    }

    #[test]
    fn local_events_pass_through_untouched() {
        let mut e = engine();
        assert_eq!(e.on_event(Ev::Move { x: 100.0, y: 100.0 }), Step::default());
        assert_eq!(
            e.on_event(Ev::Key {
                key: Key::KeyA,
                down: true
            }),
            Step::default()
        );
        assert_eq!(
            e.on_event(Ev::Button {
                button: Button::Left,
                down: true
            }),
            Step::default()
        );
    }

    #[test]
    fn remote_forwards_and_swallows_keys_buttons_wheel() {
        let mut e = engine();
        enter(&mut e);
        let s = e.on_event(Ev::Key {
            key: Key::KeyA,
            down: true,
        });
        assert_eq!(
            s.send,
            vec![(
                1,
                Msg::Key {
                    key: Key::KeyA,
                    down: true
                }
            )]
        );
        assert!(s.swallow);
        assert!(
            e.on_event(Ev::Button {
                button: Button::Left,
                down: true
            })
            .swallow
        );
        assert!(e.on_event(Ev::Wheel { dx: 0, dy: -1 }).swallow);
    }

    #[test]
    fn remote_motion_is_offset_from_center_and_warps_back() {
        let mut e = engine();
        enter(&mut e);
        let s = e.on_event(Ev::Move { x: 965.0, y: 543.0 });
        assert_eq!(s.send, vec![(1, Msg::Move { dx: 5, dy: 3 })]);
        assert_eq!(s.warp, Some((960.0, 540.0)));
        assert!(!s.swallow);
    }

    #[test]
    fn warp_echo_is_swallowed_and_not_forwarded() {
        let mut e = engine();
        enter(&mut e);
        let s = e.on_event(Ev::Move { x: 960.0, y: 540.0 });
        assert!(s.send.is_empty() && s.swallow && s.warp.is_none());
    }

    #[test]
    fn walking_back_returns_control() {
        let mut e = engine();
        enter(&mut e);
        // guest cursor starts at x=0; push left 10 px
        let s = e.on_event(Ev::Move { x: 950.0, y: 540.0 });
        assert_eq!(s.send[0].1, Msg::Leave);
        assert!(!e.is_remote());
        assert_eq!(s.warp.map(|w| w.0), Some(1917.0));
        // local again: keys pass through
        assert_eq!(
            e.on_event(Ev::Key {
                key: Key::KeyA,
                down: true
            }),
            Step::default()
        );
    }

    #[test]
    fn returned_cursor_does_not_bounce_back() {
        let mut e = engine();
        enter(&mut e);
        e.on_event(Ev::Move { x: 950.0, y: 540.0 }); // leaves
                                                     // zero-delta event at the parked position stays local
        assert_eq!(
            e.on_event(Ev::Move {
                x: 1917.0,
                y: 540.0
            }),
            Step::default()
        );
        assert!(!e.is_remote());
    }

    #[test]
    fn hotkey_returns_control() {
        let mut e = engine();
        enter(&mut e);
        e.on_event(Ev::Key {
            key: Key::ControlLeft,
            down: true,
        });
        e.on_event(Ev::Key {
            key: Key::Alt,
            down: true,
        });
        let s = e.on_event(Ev::Key {
            key: Key::Escape,
            down: true,
        });
        assert_eq!(s.send, vec![(1, Msg::Leave)]);
        assert!(s.swallow);
        assert!(!e.is_remote());
    }

    #[test]
    fn force_local_on_disconnect() {
        let mut e = engine();
        enter(&mut e);
        let s = e.force_local();
        assert_eq!(s.send, vec![(1, Msg::Leave)]);
        assert!(!e.is_remote());
        assert_eq!(e.force_local(), Step::default());
    }
}
