# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versioning: [SemVer](https://semver.org).

## [Unreleased]

### Added
- Repository foundation: dual MIT/Apache-2.0 license, PRD, architecture, roadmap, ADRs 0001-0009.
- `mono-proto`: wire messages with postcard encoding.
- `mono-net`: QUIC transport with host fingerprint pinning and pairing token.
- CI on Linux, macOS and Windows; `cargo-deny`; dependabot.
- `mono-input`: edge-crossing router (layout state machine) and pressed-key guard (release-all on disconnect).
