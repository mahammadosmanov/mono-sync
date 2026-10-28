# Current project state

Updated: 2026-10-28
Stage: Deskflow native input rebuild; Windows generator mismatch under correction
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
- Qt 6 remains unavailable locally. Authorized `brew install qt` failed during Homebrew's Ruby bootstrap over HTTP/2; an HTTP/1.1 retry was stopped after 60 seconds at 7%. No package was installed and no full Xcode installation has been attempted.
- Pinned source archive downloaded and verified against GitHub tag commit API. macOS CI passed 29/29 tests once, but DMG packaging hit a transient `hdiutil: Resource busy` error; the workflow now uses upstream's bounded retry.
- Windows CI configured and built every target. CTest reported 27/28 passing; `I18NTests` failed because its translation lookup assumes single-config paths, while Visual Studio places binaries in a `Release` subdirectory. Workflow is being changed to install pinned Ninja 1.13.2 and use single-config Ninja on Windows; all tests remain enabled.
- Latest attempted run `40724918414` was canceled after its Windows job failed and macOS remained queued. No package artifact is available from that run. Workflow change has passed local YAML parsing, `actionlint`, and `git diff --check`; dispatch and verify the next run.
- `.github/workflows/native-packages.yml` targets Windows 2022 x64 and macOS 15 ARM64 on this branch's push or manual dispatch, runs upstream tests, packages both targets, and uploads artifacts only. It does not publish a release.

## Acceptance gate

On the actual Windows host and Mac client, verify 15-minute stability and 50+ edge crossings; exact mixed 200-character typing plus modifiers/function keys; pointer click/scroll and control hotkey; held-key/button release after network disconnect; and clean reconnect. Record OS versions, monitor layout/scaling, and logs. Build/unit tests cannot replace this gate. Do not start file transfer until the user accepts input behavior.

## Next

Commit and push the Ninja generator fix, obtain passing Windows and macOS CI packages, verify package contents and signatures, then install only the distinct Deskflow app. Physical input acceptance remains gated on the actual Windows/Mac pair.
