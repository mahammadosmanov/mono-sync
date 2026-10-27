//! Host side: decides, per physical event, whether to forward it to a peer,
//! swallow it locally and/or warp the local cursor.
//!
//! While a peer owns the cursor we keep the local cursor parked at the screen
//! centre and read motion as the offset from that centre (works with plain
//! absolute positions on every OS). The warp's own echo event is swallowed.
//!
//! While remote, every report except an exact-centre echo of our own warp is
//! re-parked (warped back to the centre). Warp jitter within `HOLD_PX` of the
//! centre is re-parked but not forwarded, so it cannot push the virtual cursor
//! past the peer's back edge. Reports within `STALE_PX` of the screen edge are
//! pre-warp positions: re-parked, never forwarded. See ADR-0017.
use crate::layout::{Edge, Link, Out, PeerId, Router, Size};
use mono_proto::{Button, Key, Msg};
use std::collections::HashSet;

/// After the cursor comes back through a host edge, reports this close to that
/// edge do not cross again until the cursor is this far away from it. Queued
/// pre-warp reports sit at the edge and would otherwise re-enter (ADR-0017).
const REENTER_PX: f64 = 30.0;
/// Offsets from the parked centre below this on both axes are warp jitter.
/// Jitter seen on Windows is up to 3 px, so 4 px holds it.
const HOLD_PX: f64 = 4.0;
/// Parked reports this close to the screen edge are queued pre-warp positions
/// (Windows queues up to three pixels from the old edge).
const STALE_PX: f64 = 3.0;
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
    /// Pointer events still to trace after the last crossing.
    traced: u8,
    /// Host edge the cursor last came back through. Local reports near it do not
    /// cross until the cursor is `REENTER_PX` away from it.
    returned_via: Option<Edge>,
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
            traced: 0,
            returned_via: None,
        }
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
            if let Some(edge) = self.returned_via {
                if self.edge_distance(edge, (x, y)) < REENTER_PX {
                    // Still at the edge the cursor came back through: no crossing.
                    return Step::default();
                }
                self.returned_via = None;
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
        // Every report that is not our own echo is re-parked, stale and held ones too:
        // the OS cursor must not stay where the user pushed it (ADR-0017).
        let park = Step {
            warp: Some(c),
            ..Step::default()
        };
        let (kind, mut s) = if ox.abs() < 1.0 && oy.abs() < 1.0 {
            // Our own warp echo, exactly centred: swallow, nothing to re-park.
            (
                "echo",
                Step {
                    swallow: true,
                    ..Step::default()
                },
            )
        } else if ox.abs() >= self.host.w / 2.0 - STALE_PX
            || oy.abs() >= self.host.h / 2.0 - STALE_PX
        {
            // Queued from before the warp: re-park, never forward.
            ("stale", park)
        } else if ox.abs() < HOLD_PX && oy.abs() < HOLD_PX {
            // Jitter: re-park, never forward, so it cannot reach the exit rule.
            ("held", park)
        } else {
            let outs = self.router.motion((x, y), (ox, oy));
            let left = outs.iter().any(|o| matches!(o, Out::Leave { .. }));
            let mut s = self.apply(outs);
            // A return keeps the edge warp apply() set; only a move re-parks at the centre.
            if !left {
                s.warp = Some(c);
            }
            (if left { "leave" } else { "move" }, s)
        };
        // Every pointer report of a remote move is swallowed, leave included (Input
        // Leap relay mode): the OS must not move the cursor off the centre.
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

    /// Windows reports the parked centre with a few px of jitter, and queued
    /// reports from before the warp show the pre-warp edge (x = w-1, w-2, w-3).
    /// Every report except an exact-centre echo is re-parked; none of them may
    /// return control or be forwarded.
    #[test]
    fn host_edge_bounce_after_enter() {
        let mut e = engine();
        enter(&mut e);
        let reports = [
            (959.0, 540.0),  // jitter -1 toward the peer's back edge
            (958.0, 541.0),  // jitter -2, +1
            (957.0, 539.0),  // jitter -3, -1
            (961.0, 542.0),  // jitter +1, +2
            (1919.0, 540.0), // stale: pre-warp edge, x = width-1
            (1918.0, 540.0), // stale: pre-warp edge, x = width-2
            (1917.0, 540.0), // stale: pre-warp edge, x = width-3
            (959.0, 541.0),
        ];
        for (x, y) in reports {
            let s = e.on_event(Ev::Move { x, y });
            assert!(s.send.is_empty(), "({x}, {y}) sent {:?}", s.send);
            assert_eq!(s.warp, Some((960.0, 540.0)), "({x}, {y}) not re-parked");
            assert!(e.is_remote(), "({x}, {y}) bounced back to this computer");
        }
        // Our own warp echo: swallowed, nothing to re-park.
        let s = e.on_event(Ev::Move { x: 960.0, y: 540.0 });
        assert!(s.swallow && s.warp.is_none() && s.send.is_empty());
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

    /// Re-parking leaves no OS offset to add up, so a step under 4 px is
    /// re-parked and not forwarded: it never adds up to an exit (ADR-0017,
    /// Consequences). Only a single report 4 px past the back edge exits.
    #[test]
    fn sub_threshold_steps_are_not_summed() {
        let mut e = engine();
        enter(&mut e);
        for _ in 0..10 {
            let s = e.on_event(Ev::Move { x: 957.0, y: 540.0 });
            assert!(s.send.is_empty() && s.warp == Some((960.0, 540.0)));
            assert!(e.is_remote());
        }
        let s = e.on_event(Ev::Move { x: 956.0, y: 540.0 });
        assert_eq!(s.send[0].1, Msg::Leave);
        assert!(!e.is_remote());
    }

    #[test]
    fn first_pointer_events_after_a_crossing_are_traced() {
        let mut e = engine();
        assert!(e.on_event(Ev::Move { x: 100.0, y: 100.0 }).trace.is_none());
        enter(&mut e);
        let mut traced = Vec::new();
        for _ in 0..10 {
            // Held jitter: the OS keeps the offset, so every event stays parked.
            if let Some(line) = e.on_event(Ev::Move { x: 959.0, y: 540.0 }).trace {
                traced.push(line);
            }
        }
        assert_eq!(traced.len(), 8);
        assert_eq!(
            traced[0],
            "host 1/8 held: raw 959,540 off -1,+0 -> send 0, swallow yes, warp 960,540"
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
