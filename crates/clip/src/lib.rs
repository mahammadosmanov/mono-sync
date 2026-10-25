//! Text clipboard sync. `State` is pure echo-suppression; `spawn` is the OS glue.
use std::{
    sync::mpsc::{channel, Receiver, Sender, TryRecvError},
    thread,
    time::Duration,
};

/// Frames are capped at 64 KiB (mono-net); leave room for the envelope.
pub const MAX_BYTES: usize = 48 * 1024;

#[derive(Default)]
pub struct State {
    last: Option<String>,
}

impl State {
    /// Local clipboard now reads `current`. Returns text to send if it is new.
    pub fn local_changed(&mut self, current: Option<String>) -> Option<String> {
        let cur = current?;
        if cur.is_empty() || cur.len() > MAX_BYTES || self.last.as_deref() == Some(&cur) {
            return None;
        }
        self.last = Some(cur.clone());
        Some(cur)
    }

    /// Text arrived from the peer. Returns it if it should be written locally.
    /// Remembering it stops it echoing straight back.
    pub fn remote_arrived(&mut self, text: String) -> Option<String> {
        if text.len() > MAX_BYTES || self.last.as_deref() == Some(&text) {
            return None;
        }
        self.last = Some(text.clone());
        Some(text)
    }
}

/// Starts a watcher thread. Returns (changes to send, sink for incoming text).
pub fn spawn() -> (Receiver<String>, Sender<String>) {
    let (out_tx, out_rx) = channel::<String>();
    let (in_tx, in_rx) = channel::<String>();
    thread::spawn(move || {
        let Ok(mut cb) = arboard::Clipboard::new() else {
            eprintln!("clipboard unavailable; text sync disabled");
            return;
        };
        let mut st = State::default();
        // Do not push whatever was on the clipboard before we started.
        st.local_changed(cb.get_text().ok());
        loop {
            loop {
                match in_rx.try_recv() {
                    Ok(t) => {
                        if let Some(t) = st.remote_arrived(t) {
                            let _ = cb.set_text(t);
                        }
                    }
                    Err(TryRecvError::Empty) => break,
                    Err(TryRecvError::Disconnected) => return,
                }
            }
            if let Some(t) = st.local_changed(cb.get_text().ok()) {
                if out_tx.send(t).is_err() {
                    return;
                }
            }
            thread::sleep(Duration::from_millis(300));
        }
    });
    (out_rx, in_tx)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sends_only_new_nonempty_text() {
        let mut s = State::default();
        assert_eq!(s.local_changed(None), None);
        assert_eq!(s.local_changed(Some(String::new())), None);
        assert_eq!(s.local_changed(Some("a".into())), Some("a".into()));
        assert_eq!(s.local_changed(Some("a".into())), None);
        assert_eq!(s.local_changed(Some("b".into())), Some("b".into()));
    }

    #[test]
    fn remote_text_does_not_echo_back() {
        let mut s = State::default();
        assert_eq!(s.remote_arrived("x".into()), Some("x".into()));
        // watcher then sees the text we just wrote
        assert_eq!(s.local_changed(Some("x".into())), None);
        assert_eq!(s.remote_arrived("x".into()), None);
    }

    #[test]
    fn oversize_is_ignored() {
        let mut s = State::default();
        let big = "a".repeat(MAX_BYTES + 1);
        assert_eq!(s.local_changed(Some(big.clone())), None);
        assert_eq!(s.remote_arrived(big), None);
    }
}
