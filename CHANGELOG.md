# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versioning: [SemVer](https://semver.org).

## [Unreleased]

### Added
- Repository foundation: dual MIT/Apache-2.0 license, PRD, architecture, roadmap, ADRs 0001-0009.
- `mono-proto`: wire messages with postcard encoding.
- `mono-net`: QUIC transport with host fingerprint pinning and pairing token.
- CI on Linux, macOS and Windows; `cargo-deny`; dependabot.
- `mono-input`: edge-crossing router, host and guest engines, pressed-key guard (release-all on disconnect), rdev OS backend.
- `mono-cli`: `mono host` and `mono guest` commands (pre-alpha, not yet verified on real hardware).
- Hotkey Ctrl+Alt(Option)+Esc returns control to the host.
- `mono-clip`: text clipboard sync in both directions with echo suppression (48 KiB cap).
- `mono-net::file`: verified file transfer on its own QUIC stream, safe names, no overwrite (ADR-0011).
- ADR-0010: centre-warp motion capture.
