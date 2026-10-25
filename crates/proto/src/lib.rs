//! Wire messages. `Key`/`Button` come from rdev: they name physical keys, not
//! layout characters, so a key press maps to the same key on the other OS.
pub use rdev::{Button, Key};
use serde::{Deserialize, Serialize};

pub const VERSION: u16 = 1;

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub enum Msg {
    /// Guest -> host, first message. `token` is the pairing secret.
    Hello {
        version: u16,
        token: String,
    },
    HelloOk,
    /// Guest -> host after `HelloOk`: size of the guest's screen in its own
    /// coordinates, so the host can track the virtual cursor.
    Screen {
        w: u32,
        h: u32,
    },
    /// Cursor enters the guest at fractional position (0..1) of its screen.
    Enter {
        x: f32,
        y: f32,
    },
    Leave,
    Key {
        key: Key,
        down: bool,
    },
    Button {
        button: Button,
        down: bool,
    },
    /// Relative motion. Sent as datagram: losing one is fine.
    Move {
        dx: i32,
        dy: i32,
    },
    Wheel {
        dx: i64,
        dy: i64,
    },
    /// Clipboard text changed on the sender. Appended last: keeps old encodings valid.
    Clip {
        text: String,
    },
}

impl Msg {
    pub fn encode(&self) -> Vec<u8> {
        postcard::to_stdvec(self).expect("Msg always serializes")
    }
    pub fn decode(b: &[u8]) -> Result<Msg, postcard::Error> {
        postcard::from_bytes(b)
    }
    /// Lossy-OK messages go as datagrams; everything else must arrive in order.
    pub fn is_lossy(&self) -> bool {
        matches!(self, Msg::Move { .. })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip() {
        for m in [
            Msg::Hello {
                version: VERSION,
                token: "t".into(),
            },
            Msg::Screen { w: 2560, h: 1440 },
            Msg::Enter { x: 0.0, y: 0.5 },
            Msg::Key {
                key: Key::KeyA,
                down: true,
            },
            Msg::Button {
                button: Button::Left,
                down: false,
            },
            Msg::Move { dx: -3, dy: 7 },
            Msg::Wheel { dx: 0, dy: -120 },
            Msg::Clip {
                text: "héllo".into(),
            },
        ] {
            assert_eq!(Msg::decode(&m.encode()).unwrap(), m);
        }
    }

    #[test]
    fn move_is_small_and_lossy() {
        let m = Msg::Move { dx: 100, dy: -100 };
        assert!(m.encode().len() <= 8);
        assert!(m.is_lossy());
        assert!(!Msg::Leave.is_lossy());
    }

    #[test]
    fn garbage_rejected() {
        assert!(Msg::decode(&[0xff, 0xff, 0xff]).is_err());
    }
}
