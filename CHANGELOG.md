# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versioning: [SemVer](https://semver.org).

## [Unreleased]

### Fixed
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
