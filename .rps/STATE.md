# Current project state

Updated: 2026-10-28
Stage: Deskflow baseline installed; awaiting Windows/Mac physical input acceptance
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
- Pinned source archive downloaded and verified against GitHub tag commit API. Corrected workflow run `40731756390` built and tested the pinned source on macOS ARM64 and Windows x64; package version and basename guards passed before compilation. macOS CTest passed 29/29 and Windows CTest passed 28/28. Archive digests and ZIP CRC checks passed for both downloaded platform artifacts.
- The first packages were rejected before installation. Git history inherited from Mono Sync caused upstream's Git-derived project version to report `0.1.0.9`, and the Windows package basename appeared as `deskflow-1-win-x64`. These artifacts are preserved under `Documents/MonoSync-Deskflow-deliverables/INVALID-not-for-install-0.1.0.9/`.
- `DESKFLOW_USE_GIT_VERSION` defaults to `OFF`, preserving the pinned source version `1.27.0.0`; CI explicitly sets the CPack label and verifies version and package basename before compiling. The corrected CI guards passed.
- `.github/workflows/native-packages.yml` targets Windows 2022 x64 and macOS 15 ARM64, runs upstream tests, packages both targets, and uploads artifacts only. It does not publish a release.
- Corrected Windows MSI: `/Users/rohtolos/Documents/MonoSync-Deskflow-deliverables/Windows/Deskflow-1.27.0-Windows-x64-Setup.msi`, 15,994,759 bytes, SHA-256 `ca063db0ad9dd4e968007965f477659881c1cf4fb0b352d216d5a2a677554141`; MSI metadata identifies x64 and Deskflow. The original named package and portable ZIP remain in `Windows/payload/`.
- GitHub run: `https://github.com/mahammadosmanov/mono-sync/actions/runs/40731756390`, source revision `485092eaf2a708391425f63fe6cd5b94aeb11e97`. Windows artifact ID `12834854969`; macOS artifact ID `12833919872`; neither was expired at last check. Windows archive SHA-256 is `7804532b60db60e47038145d65f4d4e2828abec1a50c8e27ab6fb8ce14fa874d`; macOS archive SHA-256 is `b66afcfaf78029df735f507d63f22d4567abb33aa8e78d15d4d1f2e4b3e737d4`.
- Corrected macOS artifact came from the same pinned source revision. Its ARM64 app bundle reports `1.27.0.0`. The downloaded CI bundle needed deep signing with the existing Apple Development identity because nested Qt signatures had a Team ID mismatch; deep strict verification then passed. The locally installed `/Applications/Deskflow.app` passes deep strict signature verification; executable SHA-256 is `617de79427203f6e3689bde2f27287d4feb337e6af9cecfe9c87da3ddec234dc`, and `--version` reports `Deskflow: 1.27.0`.
- Deskflow's main window was visibly confirmed in CUA. System Settings > Device Control and Data Access lists Deskflow with access `off`. The user authorized this exact permission, but macOS requires Touch ID or the account password to apply it; the OS authentication sheet is open for the user. Client mode is selected; the server address is blank and Connect remains disabled. No sharing session or peer trust was configured. Previous Mono Sync app copies were moved recoverably to `~/.Trash`; configuration remains preserved.

## Acceptance gate

On the actual Windows host and Mac client, verify 15-minute stability and 50+ edge crossings; exact mixed 200-character typing plus modifiers/function keys; pointer click/scroll and control hotkey; held-key/button release after network disconnect; and clean reconnect. Record OS versions, monitor layout/scaling, and logs. Build/unit tests cannot replace this gate. Do not start file transfer until the user accepts input behavior.

## Next

Run the manual acceptance gate on the actual Windows host and Mac client. The CI builds and Mac startup are verified, but cross-device input behavior remains untested. Do not start file-transfer work until the user accepts that input gate.
