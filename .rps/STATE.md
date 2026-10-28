# Current project state

Updated: 2026-10-28
Stage: Deskflow native input rebuild; first package builds in progress
Profile: P2 (cross-device keyboard and mouse sharing)

## Goal

Replace the custom Mono Sync keyboard/mouse engine with the real Deskflow application and OS backends. Deliver Windows x64 host and Apple Silicon macOS client builds. Defer file transfer until actual two-machine input acceptance passes.

## Source and branch

- Repo: `/Users/rohtolos/Documents/GitHub/mono-sync`
- Branch: `codex/deskflow-rebuild`, based on preserved GitHub branch commit `197aad294cc7bf0efc1ba47d3dc30f6645c67fb4`.
- Deskflow source: `v1.27.0`, commit `081f6478e654a112168879e67a41ce7d7e4ae3af`; archive SHA-256 is in `docs/UPSTREAM.md`.
- Former Rust/Tauri/rdev product files are removed from this branch. The previous branch and installed Mono Sync app/config remain available for rollback; do not launch both input apps together.
- Deskflow retains its native GUI, TLS, input engine, and OS backends. File transfer is not included.

## Build environment and progress

- Local machine has CMake 4.4.3, Apple Clang 21, Xcode Command Line Tools, OpenSSL 4.0.3, and 19 GiB free at last check.
- Qt 6 was missing. Authorized `brew install qt` is in progress (Homebrew formula 6.12.0); no full Xcode installation has been attempted.
- Pinned source archive downloaded and verified against GitHub tag commit API; no compile/test result yet.
- Manual workflow `.github/workflows/native-packages.yml` targets Windows 2022 x64 and macOS 15 ARM64, uses upstream dependency setup, and uploads artifacts only. It does not publish a release.

## Acceptance gate

On the actual Windows host and Mac client, verify 15-minute stability and 50+ edge crossings; exact mixed 200-character typing plus modifiers/function keys; pointer click/scroll and control hotkey; held-key/button release after network disconnect; and clean reconnect. Record OS versions, monitor layout/scaling, and logs. Build/unit tests cannot replace this gate. Do not start file transfer until the user accepts input behavior.

## Next

Finish Qt installation, configure/build/test the pinned native app locally, dispatch the Windows/macOS artifact workflow, install only the distinct Deskflow app after verifying signature and bundle identity, then run the physical input gate when the Windows host is available.
