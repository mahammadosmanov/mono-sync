//! Ordered input queue with adjacent pointer-motion coalescing.

use crate::layout::PeerId;
use mono_proto::Msg;
use std::{
    collections::VecDeque,
    sync::{Arc, Mutex},
};
use tokio::sync::Notify;

#[derive(Default)]
struct Inner {
    state: Mutex<State>,
    ready: Notify,
}

#[derive(Default)]
struct State {
    generation: u64,
    messages: VecDeque<(PeerId, Msg)>,
}

/// Shared event queue. Use one `recv` consumer and any number of `push` producers.
#[derive(Clone, Default)]
pub struct InputQueue(Arc<Inner>);

impl InputQueue {
    /// Invalidate queued and in-flight work, then install the new engine while no
    /// engine-locked producer can pair an event with the wrong generation. The
    /// caller must hold the engine lock before calling this method.
    pub fn activate<R>(&self, install: impl FnOnce() -> R) -> R {
        {
            let mut state = self
                .0
                .state
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            state.generation = state.generation.wrapping_add(1);
            state.messages.clear();
        }
        install()
    }

    /// Current event generation, for steps that outlive the dispatch closure.
    pub fn generation(&self) -> u64 {
        self.0
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .generation
    }

    /// True while `generation` still names the active engine.
    pub fn is_current(&self, generation: u64) -> bool {
        self.generation() == generation
    }

    /// Queue one event, merging it with the adjacent tail move only when the
    /// peer matches and both summed deltas remain representable as `i32`.
    pub fn push(&self, peer: PeerId, msg: Msg) {
        let generation = self.generation();
        self.push_if_generation(generation, peer, msg);
    }

    /// Queue one event only if its engine generation is still active.
    pub fn push_if_generation(&self, generation: u64, peer: PeerId, msg: Msg) -> bool {
        let mut state = self
            .0
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if state.generation != generation {
            return false;
        }
        let messages = &mut state.messages;
        let msg = match msg {
            Msg::Move { dx, dy } => {
                let merged = messages.back_mut().is_some_and(|(last_peer, last_msg)| {
                    if *last_peer != peer {
                        return false;
                    }
                    let Msg::Move {
                        dx: last_dx,
                        dy: last_dy,
                    } = last_msg
                    else {
                        return false;
                    };
                    if !same_direction_or_zero(*last_dx, dx)
                        || !same_direction_or_zero(*last_dy, dy)
                    {
                        return false;
                    }
                    if let (Some(sum_x), Some(sum_y)) =
                        (last_dx.checked_add(dx), last_dy.checked_add(dy))
                    {
                        *last_dx = sum_x;
                        *last_dy = sum_y;
                        true
                    } else {
                        false
                    }
                });
                if merged {
                    return true;
                }
                Msg::Move { dx, dy }
            }
            other => other,
        };
        messages.push_back((peer, msg));
        drop(state);
        self.0.ready.notify_one();
        true
    }

    /// Wait for the next event. `notify_one` retains a permit if a push races
    /// between the empty check and this await, so the single consumer cannot miss it.
    pub async fn recv(&self) -> (PeerId, Msg) {
        loop {
            if let Some(message) = self
                .0
                .state
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner())
                .messages
                .pop_front()
            {
                return message;
            }
            self.0.ready.notified().await;
        }
    }
}

fn same_direction_or_zero(a: i32, b: i32) -> bool {
    a == 0 || b == 0 || a.signum() == b.signum()
}

#[cfg(test)]
mod tests {
    use super::*;
    use mono_proto::{Key, Msg};
    use std::time::Duration;

    #[tokio::test]
    async fn coalesces_motion_burst_before_key_without_losing_delta() {
        let q = InputQueue::default();
        q.push(1, Msg::Move { dx: 2, dy: 3 });
        q.push(1, Msg::Move { dx: 1, dy: 4 });
        q.push(
            1,
            Msg::Key {
                key: Key::KeyA,
                down: true,
            },
        );

        assert_eq!(q.recv().await, (1, Msg::Move { dx: 3, dy: 7 }));
        assert_eq!(
            q.recv().await,
            (
                1,
                Msg::Key {
                    key: Key::KeyA,
                    down: true
                }
            )
        );
    }

    #[tokio::test]
    async fn reliable_messages_and_peer_changes_are_coalescing_barriers() {
        let q = InputQueue::default();
        q.push(1, Msg::Move { dx: 1, dy: 0 });
        q.push(2, Msg::Move { dx: 2, dy: 0 });
        q.push(1, Msg::Move { dx: 3, dy: 0 });
        q.push(1, Msg::Leave);
        q.push(1, Msg::Move { dx: 4, dy: 0 });

        assert_eq!(q.recv().await, (1, Msg::Move { dx: 1, dy: 0 }));
        assert_eq!(q.recv().await, (2, Msg::Move { dx: 2, dy: 0 }));
        assert_eq!(q.recv().await, (1, Msg::Move { dx: 3, dy: 0 }));
        assert_eq!(q.recv().await, (1, Msg::Leave));
        assert_eq!(q.recv().await, (1, Msg::Move { dx: 4, dy: 0 }));
    }

    #[tokio::test]
    async fn overflow_keeps_another_move_instead_of_changing_total() {
        let q = InputQueue::default();
        q.push(
            1,
            Msg::Move {
                dx: i32::MAX,
                dy: i32::MIN,
            },
        );
        q.push(1, Msg::Move { dx: 1, dy: -1 });

        assert_eq!(
            q.recv().await,
            (
                1,
                Msg::Move {
                    dx: i32::MAX,
                    dy: i32::MIN
                }
            )
        );
        assert_eq!(q.recv().await, (1, Msg::Move { dx: 1, dy: -1 }));
    }

    #[tokio::test]
    async fn opposite_direction_motion_stays_separate_at_clamped_edges() {
        let q = InputQueue::default();
        q.push(1, Msg::Move { dx: -5, dy: 0 });
        q.push(1, Msg::Move { dx: 5, dy: 0 });

        assert_eq!(q.recv().await, (1, Msg::Move { dx: -5, dy: 0 }));
        assert_eq!(q.recv().await, (1, Msg::Move { dx: 5, dy: 0 }));
    }

    #[tokio::test]
    async fn activation_rejects_an_in_flight_old_step() {
        let q = InputQueue::default();
        let old_generation = q.generation();
        q.activate(|| ());

        assert!(!q.push_if_generation(old_generation, 1, Msg::Leave));
        let current = q.generation();
        assert!(q.push_if_generation(current, 1, Msg::Enter { x: 0.0, y: 0.5 }));
        assert_eq!(q.recv().await, (1, Msg::Enter { x: 0.0, y: 0.5 }));
    }

    #[tokio::test]
    async fn activation_drops_stale_events_but_later_enter_is_preserved() {
        let q = InputQueue::default();
        q.push(1, Msg::Move { dx: 9, dy: 0 });
        let engine_lock = Mutex::new(());
        {
            let _guard = engine_lock.lock().unwrap();
            q.activate(|| ());
            q.push(1, Msg::Enter { x: 0.0, y: 0.5 });
        }

        assert_eq!(q.recv().await, (1, Msg::Enter { x: 0.0, y: 0.5 }));
    }

    #[tokio::test]
    async fn push_wakes_waiting_receiver() {
        let q = InputQueue::default();
        let waiting = q.clone();
        let task = tokio::spawn(async move { waiting.recv().await });
        tokio::task::yield_now().await;
        q.push(1, Msg::Leave);

        assert_eq!(
            tokio::time::timeout(Duration::from_secs(1), task)
                .await
                .unwrap()
                .unwrap(),
            (1, Msg::Leave)
        );
    }
}
