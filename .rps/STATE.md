# Current project state

Updated: 2026-10-28
Stage: Deskflow native input rebuild; pinning package metadata before delivery
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
- Pinned source archive downloaded and verified against GitHub tag commit API. Run `40726949413` passed 29/29 macOS tests and 28/28 Windows tests; both native packages built and uploaded.
- The first packages were rejected before installation. Git history inherited from Mono Sync caused upstream's Git-derived project version to report `0.1.0.9`, and the Windows package basename appeared as `deskflow-1-win-x64`. These artifacts are preserved under `Documents/MonoSync-Deskflow-deliverables/INVALID-not-for-install-0.1.0.9/`.
- `DESKFLOW_USE_GIT_VERSION` now defaults to `OFF`, preserving the pinned source fallback `1.27.0.0`; the workflow explicitly disables Git-derived versions, quotes the CPack label, and verifies version and package basename before compiling. The CPack assertion passed against a representative config; `actionlint` and `git diff --check` pass. Actual CI verification is pending.
- `.github/workflows/native-packages.yml` targets Windows 2022 x64 and macOS 15 ARM64, runs upstream tests, packages both targets, and uploads artifacts only. It does not publish a release.

## Acceptance gate

On the actual Windows host and Mac client, verify 15-minute stability and 50+ edge crossings; exact mixed 200-character typing plus modifiers/function keys; pointer click/scroll and control hotkey; held-key/button release after network disconnect; and clean reconnect. Record OS versions, monitor layout/scaling, and logs. Build/unit tests cannot replace this gate. Do not start file transfer until the user accepts input behavior.

## Next

Commit and push the package-version fix. Obtain corrected Windows and Mac artifacts, verify their version metadata, API digests, and signatures, then install the distinct Deskflow app. Physical input acceptance remains gated on the actual Windows/Mac pair.
