//! Screen layout and the host-side routing state machine.
//!
//! The host owns the physical devices. While the cursor is on a host screen
//! events stay local. Reaching an edge linked to a peer switches to `Remote`:
//! motion is then accumulated on a virtual cursor inside the peer's screen
//! until it leaves through the edge it entered by.

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Size {
    pub w: f64,
    pub h: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Edge {
    Left,
    Right,
    Top,
    Bottom,
}

impl Edge {
    fn opposite(self) -> Edge {
        match self {
            Edge::Left => Edge::Right,
            Edge::Right => Edge::Left,
            Edge::Top => Edge::Bottom,
            Edge::Bottom => Edge::Top,
        }
    }
}

pub type PeerId = u32;

/// Pixels inside the edge where a returning cursor is placed, so a zero-delta
/// event does not bounce it straight back out.
const INSET: f64 = 2.0;

/// A peer attached to a host edge.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Link {
    pub edge: Edge,
    pub peer: PeerId,
    pub size: Size,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Out {
    /// Cursor entered `peer` at fractional position (0..1) of its screen.
    Enter { peer: PeerId, x: f32, y: f32 },
    /// Relative motion to forward to the peer.
    Move { peer: PeerId, dx: i32, dy: i32 },
    /// Cursor came back. Place the local cursor at `host_x/host_y`.
    Leave {
        peer: PeerId,
        host_x: f64,
        host_y: f64,
    },
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum State {
    Local,
    Remote { link: usize, x: f64, y: f64 },
}

pub struct Router {
    host: Size,
    links: Vec<Link>,
    state: State,
}

impl Router {
    pub fn new(host: Size, links: Vec<Link>) -> Self {
        Self {
            host,
            links,
            state: State::Local,
        }
    }

    pub fn is_remote(&self) -> bool {
        matches!(self.state, State::Remote { .. })
    }

    /// Force control back to the host (hotkey, disconnect). Returns the peer to release.
    pub fn force_local(&mut self) -> Option<PeerId> {
        match std::mem::replace(&mut self.state, State::Local) {
            State::Remote { link, .. } => Some(self.links[link].peer),
            State::Local => None,
        }
    }

    /// Feed one pointer motion: `abs` is the OS cursor position (meaningful
    /// while local), `(dx, dy)` the relative movement.
    pub fn motion(&mut self, abs: (f64, f64), d: (f64, f64)) -> Vec<Out> {
        match self.state {
            State::Local => self.local(abs, d),
            State::Remote { link, x, y } => self.remote(link, x, y, d),
        }
    }

    fn local(&mut self, (x, y): (f64, f64), (dx, dy): (f64, f64)) -> Vec<Out> {
        let at = |edge: Edge| match edge {
            // The OS clamps the cursor at the edge, so pushing against it
            // shows up as zero delta: "not moving away" counts as pushing.
            Edge::Left => x <= 0.0 && dx <= 0.0,
            Edge::Right => x >= self.host.w - 1.0 && dx >= 0.0,
            Edge::Top => y <= 0.0 && dy <= 0.0,
            Edge::Bottom => y >= self.host.h - 1.0 && dy >= 0.0,
        };
        let Some((i, l)) = self.links.iter().enumerate().find(|(_, l)| at(l.edge)) else {
            return vec![];
        };
        let (fx, fy) = (x / self.host.w, y / self.host.h);
        // Enter on the side opposite to the host edge the cursor left through.
        let (ex, ey) = match l.edge {
            Edge::Left => (1.0, fy),
            Edge::Right => (0.0, fy),
            Edge::Top => (fx, 1.0),
            Edge::Bottom => (fx, 0.0),
        };
        let (px, py) = (
            ex.clamp(0.0, 1.0) * (l.size.w - 1.0),
            ey.clamp(0.0, 1.0) * (l.size.h - 1.0),
        );
        let enter = Out::Enter {
            peer: l.peer,
            x: ex as f32,
            y: ey as f32,
        };
        self.state = State::Remote {
            link: i,
            x: px,
            y: py,
        };
        vec![enter]
    }

    fn remote(&mut self, i: usize, x: f64, y: f64, (dx, dy): (f64, f64)) -> Vec<Out> {
        let l = self.links[i];
        let (nx, ny) = (x + dx, y + dy);
        // The edge of the peer that faces the host.
        let back = l.edge.opposite();
        let exits = match back {
            Edge::Left => nx < 0.0,
            Edge::Right => nx > l.size.w - 1.0,
            Edge::Top => ny < 0.0,
            Edge::Bottom => ny > l.size.h - 1.0,
        };
        if exits {
            self.state = State::Local;
            let (fx, fy) = (
                (x / l.size.w).clamp(0.0, 1.0),
                (y / l.size.h).clamp(0.0, 1.0),
            );
            let (hx, hy) = match l.edge {
                Edge::Left => (INSET, fy * self.host.h),
                Edge::Right => (self.host.w - 1.0 - INSET, fy * self.host.h),
                Edge::Top => (fx * self.host.w, INSET),
                Edge::Bottom => (fx * self.host.w, self.host.h - 1.0 - INSET),
            };
            return vec![Out::Leave {
                peer: l.peer,
                host_x: hx,
                host_y: hy,
            }];
        }
        let cx = nx.clamp(0.0, l.size.w - 1.0);
        let cy = ny.clamp(0.0, l.size.h - 1.0);
        self.state = State::Remote {
            link: i,
            x: cx,
            y: cy,
        };
        vec![Out::Move {
            peer: l.peer,
            dx: dx.round() as i32,
            dy: dy.round() as i32,
        }]
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

    fn router(edge: Edge) -> Router {
        Router::new(
            HOST,
            vec![Link {
                edge,
                peer: 7,
                size: GUEST,
            }],
        )
    }

    #[test]
    fn middle_of_screen_stays_local() {
        let mut r = router(Edge::Right);
        assert!(r.motion((500.0, 500.0), (5.0, 0.0)).is_empty());
        assert!(!r.is_remote());
    }

    #[test]
    fn pushing_against_clamped_edge_enters() {
        let mut r = router(Edge::Right);
        assert_eq!(r.motion((1919.0, 0.0), (0.0, 0.0)).len(), 1);
    }

    #[test]
    fn moving_away_from_edge_stays_local() {
        let mut r = router(Edge::Right);
        assert!(r.motion((1919.0, 500.0), (-5.0, 0.0)).is_empty());
    }

    #[test]
    fn right_edge_enters_guest_left_side_proportionally() {
        let mut r = router(Edge::Right);
        let out = r.motion((1919.0, 540.0), (3.0, 0.0));
        assert_eq!(
            out,
            vec![Out::Enter {
                peer: 7,
                x: 0.0,
                y: 0.5
            }]
        );
        assert!(r.is_remote());
    }

    #[test]
    fn left_edge_enters_guest_right_side() {
        let mut r = router(Edge::Left);
        let out = r.motion((0.0, 270.0), (-1.0, 0.0));
        assert_eq!(
            out,
            vec![Out::Enter {
                peer: 7,
                x: 1.0,
                y: 0.25
            }]
        );
    }

    #[test]
    fn bottom_edge_enters_guest_top() {
        let mut r = router(Edge::Bottom);
        let out = r.motion((960.0, 1079.0), (0.0, 2.0));
        assert_eq!(
            out,
            vec![Out::Enter {
                peer: 7,
                x: 0.5,
                y: 0.0
            }]
        );
    }

    #[test]
    fn remote_forwards_motion_then_returns_through_same_edge() {
        let mut r = router(Edge::Right);
        r.motion((1919.0, 540.0), (3.0, 0.0)); // enter at guest (0, ~249.5)
        assert_eq!(
            r.motion((0.0, 0.0), (10.0, 4.0)),
            vec![Out::Move {
                peer: 7,
                dx: 10,
                dy: 4
            }]
        );
        // walk back left past guest x=0
        let out = r.motion((0.0, 0.0), (-50.0, 0.0));
        assert_eq!(out.len(), 1);
        match out[0] {
            Out::Leave {
                peer,
                host_x,
                host_y,
            } => {
                assert_eq!(peer, 7);
                assert_eq!(host_x, 1917.0);
                assert!((host_y - (253.5 / 500.0) * 1080.0).abs() < 1.0);
            }
            o => panic!("unexpected {o:?}"),
        }
        assert!(!r.is_remote());
    }

    #[test]
    fn remote_clamps_on_other_edges() {
        let mut r = router(Edge::Right);
        r.motion((1919.0, 540.0), (3.0, 0.0));
        // push far right: virtual cursor clamps, no Leave
        let out = r.motion((0.0, 0.0), (5000.0, 0.0));
        assert!(matches!(out[0], Out::Move { .. }));
        assert!(r.is_remote());
    }

    #[test]
    fn force_local_returns_peer_once() {
        let mut r = router(Edge::Right);
        r.motion((1919.0, 540.0), (3.0, 0.0));
        assert_eq!(r.force_local(), Some(7));
        assert_eq!(r.force_local(), None);
        assert!(!r.is_remote());
    }

    #[test]
    fn two_links_pick_the_matching_edge() {
        let mut r = Router::new(
            HOST,
            vec![
                Link {
                    edge: Edge::Left,
                    peer: 1,
                    size: GUEST,
                },
                Link {
                    edge: Edge::Right,
                    peer: 2,
                    size: GUEST,
                },
            ],
        );
        assert_eq!(
            r.motion((1919.0, 0.0), (1.0, 0.0)),
            vec![Out::Enter {
                peer: 2,
                x: 0.0,
                y: 0.0
            }]
        );
    }
}
