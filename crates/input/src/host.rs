//! Host side: decides, per physical event, whether to forward it to a peer,
//! swallow it locally and/or warp the local cursor.
//!
//! While a peer owns the cursor we keep the local cursor parked at the screen
//! centre and read motion as the offset from that centre (works with plain
//! absolute positions on every OS). The rules follow Input Leap's primary side,
//! as read from its source for behaviour; the code is independent (ADR-0018):
//!
//! - Every non-zero offset is forwarded and the peer accumulates it. There is
//!   no hold threshold.
//! - An offset that would take the centred cursor within `BOGUS_PX` of an edge
//!   is bogus (a queued pre-warp report or a warp artefact): dropped.
//! - A report exactly at the centre is the echo of one of our warps, or a zero
//!   move. Nothing is sent and nothing is warped.
//! - Every other remote report re-parks the cursor at the centre.
//! - Moving past the peer's back edge returns control. The host cursor is placed
//!   on its own edge, and ADR-0017's hysteresis keeps it from crossing again.
use crate::layout::{Edge, Link, Out, PeerId, Router, Size};
use mono_proto::{Button, Key, Msg};
use std::collections::HashSet;

/// After the cursor comes back through a host edge, reports this close to that
/// edge do not cross again until the cursor is this far away from it (ADR-0017).
const REENTER_PX: f64 = 30.0;
/// Input Leap's bogus zone: an offset from the parked centre that would take the
/// centred cursor to within this many px of a screen edge is dropped.
const BOGUS_PX: f64 = 10.0;
/// Most warp echoes awaited at once. An OS that never echoes must not grow the
/// count without bound.
const ECHO_CAP: u8 = 8;
/// Pointer events after each crossing that get a diagnostic line.
const TRACE_EVENTS: u8 = 8;

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
    /// One diagnostic line, set for the first pointer events after a crossing.
    pub trace: Option<String>,
}

pub struct HostEngine {
    host: Size,
    links: Vec<Link>,
    router: Router,
    last: (f64, f64),
    peer: Option<PeerId>,
    down: HashSet<Key>,
    /// Physical inputs passed through locally. Do not hand off while any remain held.
    local_keys: HashSet<Key>,
    local_buttons: HashSet<Button>,
    /// Pointer events still to trace after the last crossing.
    traced: u8,
    /// Host edge the cursor last came back through. Local reports near it do not
    /// cross until the cursor is `REENTER_PX` away from it.
    returned_via: Option<Edge>,
    /// Warps to the centre whose echo has not arrived yet (saturates at `ECHO_CAP`).
    echoes: u8,
}

impl HostEngine {
    pub fn new(host: Size, links: Vec<Link>) -> Self {
        Self {
            host,
            links: links.clone(),
            router: Router::new(host, links),
            last: (host.w / 2.0, host.h / 2.0),
            peer: None,
            down: HashSet::new(),
            local_keys: HashSet::new(),
            local_buttons: HashSet::new(),
            traced: 0,
            returned_via: None,
            echoes: 0,
        }
    }

    /// One warp to the centre was made, so its echo is expected.
    fn expect_echo(&mut self) {
        self.echoes = (self.echoes + 1).min(ECHO_CAP);
    }

    /// True when `offset` from the parked centre would take the centred cursor to
    /// within `BOGUS_PX` of a screen edge (Input Leap's bogus-zone test).
    fn is_bogus(&self, (ox, oy): (f64, f64)) -> bool {
        let c = self.center();
        let right = self.host.w - c.0;
        let bottom = self.host.h - c.1;
        ox > right - BOGUS_PX
            || -ox > c.0 - BOGUS_PX
            || oy > bottom - BOGUS_PX
            || -oy > c.1 - BOGUS_PX
    }

    /// Distance in px from the host screen edge `edge`, measured at `(x, y)`.
    fn edge_distance(&self, edge: Edge, (x, y): (f64, f64)) -> f64 {
        match edge {
            Edge::Left => x,
            Edge::Right => self.host.w - 1.0 - x,
            Edge::Top => y,
            Edge::Bottom => self.host.h - 1.0 - y,
        }
    }

    pub fn is_remote(&self) -> bool {
        self.peer.is_some()
    }

    /// Carry state that belongs to locally held devices across a layout replacement.
    pub(crate) fn inherit_local_state(&mut self, previous: &Self) {
        self.last = previous.last;
        self.returned_via = previous.returned_via;
        self.local_keys.clone_from(&previous.local_keys);
        self.local_buttons.clone_from(&previous.local_buttons);
    }

    fn center(&self) -> (f64, f64) {
        ((self.host.w / 2.0).floor(), (self.host.h / 2.0).floor())
    }

    pub fn on_event(&mut self, ev: Ev) -> Step {
        match ev {
            Ev::Move { x, y } => self.on_move(x, y),
            Ev::Key { key, down } => self.on_key(key, down),
            Ev::Button { button, down } => self.on_button(button, down),
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
        self.echoes = 0;
        self.last = self.center();
        Step {
            send: vec![(peer, Msg::Leave)],
            warp: Some(self.center()),
            ..Step::default()
        }
    }

    fn forward(&mut self, m: Msg) -> Step {
        match self.peer {
            Some(p) => Step {
                send: vec![(p, m)],
                swallow: true,
                ..Step::default()
            },
            None => Step::default(),
        }
    }

    fn on_key(&mut self, key: Key, down: bool) -> Step {
        if self.peer.is_none() {
            if down {
                self.local_keys.insert(key);
            } else {
                self.local_keys.remove(&key);
            }
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
            && (self.down.contains(&Key::ControlLeft) || self.down.contains(&Key::ControlRight))
            && (self.down.contains(&Key::Alt) || self.down.contains(&Key::AltGr));
        if hotkey {
            let mut s = self.force_local();
            s.swallow = true;
            return s;
        }
        self.forward(Msg::Key { key, down })
    }

    fn on_button(&mut self, button: Button, down: bool) -> Step {
        if self.peer.is_some() {
            return self.forward(Msg::Button { button, down });
        }
        if down {
            self.local_buttons.insert(button);
        } else {
            self.local_buttons.remove(&button);
        }
        Step::default()
    }

    fn on_move(&mut self, x: f64, y: f64) -> Step {
        let Some(_) = self.peer else {
            let d = (x - self.last.0, y - self.last.1);
            self.last = (x, y);
            if let Some(edge) = self.returned_via {
                if self.edge_distance(edge, (x, y)) < REENTER_PX {
                    // Still at the edge the cursor came back through: no crossing.
                    return Step::default();
                }
                self.returned_via = None;
            }
            if !self.local_keys.is_empty() || !self.local_buttons.is_empty() {
                return Step::default();
            }
            let outs = self.router.motion((x, y), d);
            let mut step = self.apply(outs);
            if self.peer.is_some() {
                // The crossing move never reaches the OS (Input Leap relay mode).
                step.swallow = true;
                self.traced = TRACE_EVENTS;
            }
            return step;
        };
        let c = self.center();
        let (ox, oy) = (x - c.0, y - c.1);
        let (kind, mut s) = if ox == 0.0 && oy == 0.0 {
            // On the centre: the echo of one of our warps if one is awaited, else a
            // zero move. The cursor is already parked, so nothing is sent or warped.
            let kind = if self.echoes > 0 {
                self.echoes -= 1;
                "echo"
            } else {
                "centre"
            };
            (kind, Step::default())
        } else if self.is_bogus((ox, oy)) {
            // Queued from before a warp, or a warp artefact: dropped and re-parked.
            (
                "bogus",
                Step {
                    warp: Some(c),
                    ..Step::default()
                },
            )
        } else {
            let outs = self.router.motion((x, y), (ox, oy));
            let left = outs.iter().any(|o| matches!(o, Out::Leave { .. }));
            let mut s = self.apply(outs);
            // A return keeps the edge warp apply() set; any other move re-parks.
            if !left {
                s.warp = Some(c);
            }
            (if left { "leave" } else { "move" }, s)
        };
        // Each warp to the centre expects one echo back.
        if s.warp == Some(c) {
            self.expect_echo();
        }
        // Every remote pointer event is swallowed, leave included, so the OS never
        // moves the cursor off the centre (Input Leap relay mode).
        s.swallow = true;
        if self.traced > 0 {
            let n = TRACE_EVENTS - self.traced + 1;
            self.traced -= 1;
            let warp = match s.warp {
                Some((wx, wy)) => format!("{wx:.0},{wy:.0}"),
                None => "no".to_string(),
            };
            let swallow = if s.swallow { "yes" } else { "no" };
            s.trace = Some(format!(
                "host {n}/{TRACE_EVENTS} {kind}: raw {x:.0},{y:.0} off {ox:+.0},{oy:+.0} \
                 -> send {}, swallow {swallow}, warp {warp}",
                s.send.len()
            ));
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
                    // Park before the peer hears Enter (ADR-0018 (e)).
                    s.warp = Some(self.center());
                    self.expect_echo();
                }
                Out::Move { peer, dx, dy } => s.send.push((peer, Msg::Move { dx, dy })),
                Out::Leave {
                    peer,
                    host_x,
                    host_y,
                } => {
                    self.peer = None;
                    self.down.clear();
                    self.echoes = 0;
                    self.last = (host_x, host_y);
                    self.returned_via = self.links.iter().find(|l| l.peer == peer).map(|l| l.edge);
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
        assert!(s.swallow, "crossing move reached the OS");
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
        assert!(s.swallow);
    }

    /// While remote the OS must never see a pointer move: it would pull the
    /// cursor back to the edge (Input Leap relay mode).
    #[test]
    fn remote_move_is_swallowed_and_sent_as_offset() {
        let mut e = engine();
        enter(&mut e);
        let s = e.on_event(Ev::Move { x: 965.0, y: 543.0 });
        assert!(s.swallow, "remote move reached the OS");
        assert_eq!(s.warp, Some((960.0, 540.0)));
        assert_eq!(s.send, vec![(1, Msg::Move { dx: 5, dy: 3 })]);
    }

    #[test]
    fn warp_echo_is_swallowed_and_not_forwarded() {
        let mut e = engine();
        enter(&mut e);
        let s = e.on_event(Ev::Move { x: 960.0, y: 540.0 });
        assert!(s.send.is_empty() && s.swallow && s.warp.is_none());
    }

    /// The echo the OS reports right after the crossing warp: swallowed, not
    /// sent, and the cursor stays with the peer.
    #[test]
    fn echo_right_after_crossing_is_swallowed_and_sends_nothing() {
        let mut e = engine();
        enter(&mut e);
        let s = e.on_event(Ev::Move { x: 960.0, y: 540.0 });
        assert!(s.swallow);
        assert!(s.send.is_empty());
        assert!(e.is_remote());
    }

    /// Only an exact match with the warp target is an echo, and each warp to the
    /// centre expects exactly one.
    #[test]
    fn exact_echo_consumes_one_warp_and_centred_zero_moves_do_nothing() {
        let mut e = engine();
        enter(&mut e); // the crossing warp awaits its echo
        assert_eq!(e.echoes, 1);
        let s = e.on_event(Ev::Move { x: 960.0, y: 540.0 });
        assert!(s.send.is_empty() && s.swallow && s.warp.is_none());
        assert_eq!(e.echoes, 0);

        e.on_event(Ev::Move { x: 965.0, y: 543.0 }); // real move: warps, awaits one
        assert_eq!(e.echoes, 1);
        assert!(e.on_event(Ev::Move { x: 960.0, y: 540.0 }).send.is_empty());
        assert_eq!(e.echoes, 0);

        // Nothing awaited: a centred report is a zero move, so no warp either.
        let s = e.on_event(Ev::Move { x: 960.0, y: 540.0 });
        assert!(s.send.is_empty() && s.warp.is_none() && s.swallow);
    }

    /// The echo must match exactly. A landing 1 px off is real motion: forwarded
    /// and re-parked (ADR-0018 has no tolerance band).
    #[test]
    fn off_by_one_landing_is_forwarded_not_treated_as_echo() {
        let mut e = engine();
        enter(&mut e);
        let s = e.on_event(Ev::Move { x: 961.0, y: 540.0 });
        assert_eq!(s.send, vec![(1, Msg::Move { dx: 1, dy: 0 })]);
        assert_eq!(s.warp, Some((960.0, 540.0)));
    }

    /// An OS that never echoes must not grow the count without bound.
    #[test]
    fn unanswered_warps_are_capped() {
        let mut e = engine();
        enter(&mut e);
        for _ in 0..(3 * ECHO_CAP) {
            e.on_event(Ev::Move { x: 961.0, y: 540.0 });
        }
        assert_eq!(e.echoes, ECHO_CAP);
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
        assert!(s.swallow, "leaving move reached the OS");
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

    /// Pushing past the back edge returns control with the cursor on the host
    /// edge (INSET px inside), not at the centre. Reports queued at that edge
    /// afterwards must not cross again (ADR-0017).
    #[test]
    fn return_warps_to_host_edge_and_queued_reports_do_not_recross() {
        let mut e = engine();
        enter(&mut e);
        let s = e.on_event(Ev::Move { x: 950.0, y: 540.0 });
        assert_eq!(s.send, vec![(1, Msg::Leave)]);
        let (wx, wy) = s.warp.expect("return warps the cursor");
        assert_eq!(wx, 1917.0, "not the host edge");
        assert!(
            (wy - 540.0).abs() < 2.0,
            "y {wy} is not where the cursor left"
        );
        assert!(!e.is_remote());
        for (x, y) in [(1919.0, 540.0), (1918.0, 540.0)] {
            let s = e.on_event(Ev::Move { x, y });
            assert_eq!(s, Step::default(), "({x}, {y}) produced a crossing or warp");
            assert!(!e.is_remote(), "({x}, {y}) crossed again");
        }
    }

    /// After a return, reports within 30 px of the edge do not cross. Moving
    /// at least 30 px away from it and back does (ADR-0017).
    #[test]
    fn returned_cursor_must_move_away_before_it_can_cross_again() {
        let mut e = engine();
        enter(&mut e);
        e.on_event(Ev::Move { x: 950.0, y: 540.0 }); // returns at x = 1917
        for x in [1919.0, 1915.0, 1900.0, 1890.0] {
            let s = e.on_event(Ev::Move { x, y: 540.0 });
            assert_eq!(s, Step::default(), "x={x} (within 30 px) produced {s:?}");
            assert!(!e.is_remote(), "x={x} (within 30 px) crossed");
        }
        // 30 px from the edge: the block lifts and the next push crosses.
        assert_eq!(
            e.on_event(Ev::Move {
                x: 1889.0,
                y: 540.0
            }),
            Step::default()
        );
        let s = e.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        });
        assert!(matches!(s.send[0].1, Msg::Enter { .. }), "no crossing");
        assert_eq!(s.warp, Some((960.0, 540.0)));
        assert!(e.is_remote());
    }

    /// Exit through a bottom link: the cursor lands on the host's bottom edge, not
    /// the centre, and holds for 30 px there (ADR-0017 on another edge).
    #[test]
    fn exit_on_a_bottom_link_warps_to_the_bottom_edge_and_holds_30px() {
        let mut e = HostEngine::new(
            HOST,
            vec![Link {
                edge: Edge::Bottom,
                peer: 1,
                size: GUEST,
            }],
        );
        e.on_event(Ev::Move {
            x: 960.0,
            y: 1000.0,
        });
        let s = e.on_event(Ev::Move {
            x: 960.0,
            y: 1079.0,
        });
        assert!(matches!(s.send[0].1, Msg::Enter { .. }));
        assert_eq!(s.warp, Some((960.0, 540.0)));
        assert!(e.is_remote());
        // The peer's entry is its top edge (y = 0), so a move of -1 passes it.
        let s = e.on_event(Ev::Move { x: 960.0, y: 539.0 });
        assert_eq!(s.send[0].1, Msg::Leave);
        let (wx, wy) = s.warp.expect("return warps the cursor");
        assert_eq!(wy, 1077.0, "not the host's bottom edge");
        assert!(
            (wx - 959.0).abs() < 1.0,
            "x {wx} moved off the crossing point"
        );
        assert!(!e.is_remote());
        for y in [1077.0, 1060.0, 1050.0] {
            let s = e.on_event(Ev::Move { x: 960.0, y });
            assert_eq!(s, Step::default(), "y={y} within 30 px crossed");
            assert!(!e.is_remote());
        }
        // 30 px away the block lifts, and coming back down crosses again.
        e.on_event(Ev::Move {
            x: 960.0,
            y: 1049.0,
        });
        let s = e.on_event(Ev::Move {
            x: 960.0,
            y: 1079.0,
        });
        assert!(matches!(s.send[0].1, Msg::Enter { .. }), "no crossing");
        assert!(e.is_remote());
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
    fn hotkey_accepts_right_control_and_right_alt() {
        for control in [Key::ControlLeft, Key::ControlRight] {
            for alt in [Key::Alt, Key::AltGr] {
                let mut e = engine();
                enter(&mut e);
                e.on_event(Ev::Key {
                    key: control,
                    down: true,
                });
                e.on_event(Ev::Key {
                    key: alt,
                    down: true,
                });
                let s = e.on_event(Ev::Key {
                    key: Key::Escape,
                    down: true,
                });
                assert_eq!(s.send, vec![(1, Msg::Leave)], "{control:?} + {alt:?}");
                assert!(s.swallow);
                assert!(!e.is_remote());
            }
        }
    }

    #[test]
    fn local_key_must_be_released_before_crossing() {
        let mut e = engine();
        e.on_event(Ev::Move {
            x: 1000.0,
            y: 540.0,
        });
        e.on_event(Ev::Key {
            key: Key::ControlLeft,
            down: true,
        });
        let blocked = e.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        });
        assert!(blocked.send.is_empty());
        assert!(!blocked.swallow && !e.is_remote());

        e.on_event(Ev::Key {
            key: Key::ControlLeft,
            down: false,
        });
        let crossed = e.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        });
        assert!(matches!(crossed.send[0].1, Msg::Enter { .. }));
        assert!(e.is_remote());
    }

    #[test]
    fn local_mouse_button_must_be_released_before_crossing() {
        let mut e = engine();
        e.on_event(Ev::Move {
            x: 1000.0,
            y: 540.0,
        });
        e.on_event(Ev::Button {
            button: Button::Left,
            down: true,
        });
        let blocked = e.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        });
        assert!(blocked.send.is_empty());
        assert!(!blocked.swallow && !e.is_remote());

        e.on_event(Ev::Button {
            button: Button::Left,
            down: false,
        });
        let crossed = e.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        });
        assert!(matches!(crossed.send[0].1, Msg::Enter { .. }));
        assert!(e.is_remote());
    }

    #[test]
    fn repeated_down_then_one_up_and_unpaired_up_leave_local_state_clear() {
        let mut e = engine();
        for _ in 0..2 {
            e.on_event(Ev::Key {
                key: Key::ShiftLeft,
                down: true,
            });
        }
        e.on_event(Ev::Key {
            key: Key::ShiftLeft,
            down: false,
        });
        e.on_event(Ev::Button {
            button: Button::Right,
            down: false,
        });

        let crossed = e.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        });
        assert!(matches!(crossed.send[0].1, Msg::Enter { .. }));
    }

    #[test]
    fn remote_hotkey_release_events_do_not_leave_local_inputs_held() {
        let mut e = engine();
        enter(&mut e);
        for key in [Key::ControlLeft, Key::AltGr] {
            e.on_event(Ev::Key { key, down: true });
        }
        let s = e.on_event(Ev::Key {
            key: Key::Escape,
            down: true,
        });
        assert!(s.send.iter().any(|(_, msg)| matches!(msg, Msg::Leave)));
        for key in [Key::ControlLeft, Key::AltGr, Key::Escape] {
            e.on_event(Ev::Key { key, down: false });
        }
        e.on_event(Ev::Move {
            x: 1000.0,
            y: 540.0,
        });
        let crossed = e.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        });
        assert!(matches!(crossed.send[0].1, Msg::Enter { .. }));
    }

    /// Crossing: the crossing move is swallowed, the host cursor is parked at the
    /// centre (the entry point), and Enter names the peer's entry edge.
    #[test]
    fn crossing_move_is_swallowed_and_warps_to_the_entry_point() {
        let mut e = engine();
        e.on_event(Ev::Move {
            x: 1000.0,
            y: 540.0,
        });
        let s = e.on_event(Ev::Move {
            x: 1919.0,
            y: 540.0,
        });
        assert_eq!(s.send, vec![(1, Msg::Enter { x: 0.0, y: 0.5 })]);
        assert_eq!(s.warp, Some((960.0, 540.0)));
        assert!(s.swallow);
    }

    /// Reports queued from before the warp at the old edge are bogus: they never
    /// return control, and the peer gets only real motion afterwards.
    #[test]
    fn queued_edge_reports_after_enter_do_not_bounce_back() {
        let mut e = engine();
        enter(&mut e);
        for x in [1919.0, 1918.0, 1917.0] {
            let s = e.on_event(Ev::Move { x, y: 540.0 });
            assert!(s.send.is_empty(), "x={x} sent {:?}", s.send);
            assert_eq!(s.warp, Some((960.0, 540.0)), "x={x} not re-parked");
            assert!(e.is_remote(), "x={x} bounced back to this computer");
        }
        // Our crossing warp's echo arrives after them.
        assert!(e.on_event(Ev::Move { x: 960.0, y: 540.0 }).send.is_empty());
        let s = e.on_event(Ev::Move { x: 961.0, y: 541.0 });
        assert_eq!(s.send, vec![(1, Msg::Move { dx: 1, dy: 1 })]);
        assert!(e.is_remote());
    }

    /// A stale report at any of the last three pixels re-parks on its own, with
    /// no Leave, straight after entry.
    #[test]
    fn stale_edge_reports_repark_without_leaving() {
        for x in [1919.0, 1918.0, 1917.0] {
            let mut e = engine();
            enter(&mut e);
            let s = e.on_event(Ev::Move { x, y: 540.0 });
            assert_eq!(s.warp, Some((960.0, 540.0)), "x={x} not re-parked");
            assert!(s.send.is_empty(), "x={x} sent {:?}", s.send);
            assert!(e.is_remote(), "x={x} returned control");
        }
    }

    /// Every non-zero offset is forwarded, however small, and the peer adds them
    /// up. The virtual cursor starts at the peer's left edge, so it exits only once
    /// the 1 px steps have summed back past that edge (Input Leap's accumulation).
    #[test]
    fn one_pixel_moves_are_forwarded_and_accumulate() {
        let mut e = engine();
        enter(&mut e);
        for _ in 0..10 {
            let s = e.on_event(Ev::Move { x: 961.0, y: 540.0 });
            assert_eq!(s.send, vec![(1, Msg::Move { dx: 1, dy: 0 })]);
            assert_eq!(s.warp, Some((960.0, 540.0)));
        }
        for _ in 0..10 {
            let s = e.on_event(Ev::Move { x: 959.0, y: 540.0 });
            assert_eq!(s.send, vec![(1, Msg::Move { dx: -1, dy: 0 })]);
            assert!(
                e.is_remote(),
                "exited before the steps summed back to the edge"
            );
        }
        // The virtual cursor is at the peer's left edge (0): one more step passes it.
        let s = e.on_event(Ev::Move { x: 959.0, y: 540.0 });
        assert_eq!(s.send[0].1, Msg::Leave);
        assert!(!e.is_remote());
    }

    /// Input Leap's bogus zone: an offset that would take the centred cursor to
    /// within 10 px of an edge is dropped and re-parked. It is checked before the
    /// exit rule, so a bogus move toward the peer's back edge does not return control.
    #[test]
    fn move_into_the_bogus_zone_is_ignored() {
        let mut e = engine();
        enter(&mut e);
        // 951 px right of the centre: 9 px from the host's right edge.
        let s = e.on_event(Ev::Move {
            x: 1911.0,
            y: 540.0,
        });
        assert!(s.send.is_empty(), "bogus move sent {:?}", s.send);
        assert_eq!(s.warp, Some((960.0, 540.0)));
        // 951 px left of the centre would pass the peer's back edge; the zone drops it first.
        let s = e.on_event(Ev::Move { x: 9.0, y: 540.0 });
        assert!(s.send.is_empty() && s.warp == Some((960.0, 540.0)));
        assert!(e.is_remote(), "bogus move returned control");
        // 531 px below the centre: 9 px from the host's bottom edge.
        let s = e.on_event(Ev::Move {
            x: 960.0,
            y: 1071.0,
        });
        assert!(s.send.is_empty() && e.is_remote());
        // Just outside the zone is real motion: 950 px right is forwarded.
        let s = e.on_event(Ev::Move {
            x: 1910.0,
            y: 540.0,
        });
        assert_eq!(s.send, vec![(1, Msg::Move { dx: 950, dy: 0 })]);
    }

    #[test]
    fn first_pointer_events_after_a_crossing_are_traced() {
        let mut e = engine();
        assert!(e.on_event(Ev::Move { x: 100.0, y: 100.0 }).trace.is_none());
        enter(&mut e);
        let mut traced = Vec::new();
        for _ in 0..10 {
            if let Some(line) = e.on_event(Ev::Move { x: 961.0, y: 540.0 }).trace {
                traced.push(line);
            }
        }
        assert_eq!(traced.len(), 8);
        assert_eq!(
            traced[0],
            "host 1/8 move: raw 961,540 off +1,+0 -> send 1, swallow yes, warp 960,540"
        );
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
