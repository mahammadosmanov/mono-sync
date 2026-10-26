# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versioning: [SemVer](https://semver.org).

## [Unreleased]

### Changed
- macOS: the app asks for Accessibility and Input Monitoring itself and starts sharing as soon as they are granted (no restart needed for the host).
- Windows guest without a mouse: the guest enables Windows MouseKeys while the cursor is on it, like Input Leap, so Windows draws the pointer. MouseKeys is restored when the cursor leaves (ADR-0015). The self-drawn cursor and its setting are removed (ADR-0014 superseded).

### Fixed
- macOS: moving the mouse while a button is held (drag) was not forwarded, so the Mac cursor moved instead. Dragged events are now forwarded (vendored rdev patch).
- macOS: capture stopped for good after macOS disabled the event tap (slow callback, secure input). The tap is re-enabled. The middle mouse button is captured and injected too (vendored rdev patch).
- Windows guest: the pointer could disappear while typing. Hide-pointer-while-typing is turned off while the cursor is on the guest (restored when it leaves). MouseKeys and that setting are checked every 500 ms instead of 2 s and put back if changed; a pointer Windows reports hidden is woken with a 1 px move (at most once a second).
- Windows guest: when UAC, the lock screen or another secure screen blocks input, the log shows one "input paused" note and one "Input resumed" note, instead of an error line per event.
- Windows: injected cursor landed on the wrong place or monitor (SendInput absolute coordinates); now placed with SetCursorPos in screen pixels (vendored rdev patch).
- Saving host settings again no longer fails with "address already in use"; the listener releases its port first and rebinding retries.
- Log shows when the cursor moves to or returns from the other computer.
- macOS: app crashed on the first key press (rdev read the typed character off the main thread). Vendored rdev with a patch (vendor/rdev/MONO-SYNC-PATCH.md).

### Added
- Repository foundation: dual MIT/Apache-2.0 license, PRD, architecture, roadmap, ADRs 0001-0009.
- `mono-proto`: wire messages with postcard encoding.
- `mono-net`: QUIC transport with host fingerprint pinning and pairing token.
- CI on Linux, macOS and Windows; `cargo-deny`; dependabot.
- `mono-input`: edge-crossing router, host and guest engines, pressed-key guard (release-all on disconnect), rdev OS backend.
- `mono-cli`: `mono host` and `mono guest` commands (pre-alpha, not yet verified on real hardware).
- Hotkey Ctrl+Alt(Option)+Esc returns control to the host.
- `mono-clip`: text clipboard sync in both directions with echo suppression (48 KiB cap).
- Desktop app (ADR-0013): native window and tray icon (Tauri 2) with role, direction, pairing, file send (drag and drop or picker), status and log. Closing the window keeps sharing in the tray. Windows installer (per user) and portable exe, macOS dmg.
- `mono-core`: settings, identity and host/guest loops shared by the app and the CLI; in-process loopback integration test.
- Persistent identity and settings: the pairing code survives restarts. Guest reconnects automatically; a reconnecting guest replaces a stale connection immediately.
- `mono-net::file`: verified file transfer on its own QUIC stream, safe names, no overwrite (ADR-0011).
- ADR-0010: centre-warp motion capture.
- Windows host: the app checks that Windows Firewall lets mono-sync receive connections, and offers one button that adds the rule after one UAC approval. Without the rule the guest timed out with no message (ADR-0016).
