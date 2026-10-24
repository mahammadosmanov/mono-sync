# Testing

## Automated (CI, every PR)

`cargo fmt --check`, `cargo clippy -D warnings`, `cargo test --workspace` on
Linux, macOS and Windows; `cargo deny check`.

Covered: protocol round trips, malformed input rejection, QUIC sessions on
loopback (reliable and datagram paths), wrong token and wrong fingerprint refusal.

## Manual cross-OS checklist (per milestone)

CI cannot grant OS input permissions or move a real cursor, so these run by
hand on real machines before tagging. Record date, OS versions, network type.

### M1 core control

Setup: see README "Try it". Record `mono --version`, OS versions, network type.
Also verify at start: the screen size each side prints/uses matches its real
point resolution (Retina and Windows DPI scaling are the likely failures).
- [ ] macOS host → Windows guest: cursor crosses edge, move/click/scroll work.
- [ ] Windows host → macOS guest: same.
- [ ] Letters, digits, punctuation, arrows, F-keys, Esc/Tab/Enter/Backspace.
- [ ] Shortcuts: copy/paste/select-all use the guest's convention as configured.
- [ ] Hold a key, then pull the network cable: key is released on the guest.
- [ ] Kill the host process while a mouse button is held: button released.
- [ ] Hotkey returns control to host.
- [ ] Different DPI/resolution screens: cursor lands at the proportional position.

### Later milestones
Pairing/discovery (M2), clipboard and file hash verification (M3), UI flows (M4),
latency/throughput benchmark protocol and results (M5).

## Known unautomatable limits
UAC secure desktop, lock screen and macOS login window cannot receive injected input.
