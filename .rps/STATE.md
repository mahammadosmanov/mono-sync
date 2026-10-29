# Current project state

Updated: 2026-10-29
Stage: Per-user Windows Setup packaging in progress; physical input acceptance remains pending
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
- The MSI requests elevation because `deploy/windows/wix-patch.xml.in` installs an automatic Windows service, adds machine-wide firewall exceptions, and launches the GUI from the installer. The MSI is kept under `Documents/MonoSync-Deskflow-deliverables/Windows/Admin-required-MSI-not-default/` and is not the standard-user download.
- An earlier portable ZIP with SHA-256 `036c8f301d9c3a523fa75bc5d76fb53015fe4bb0d19b6c743f4c9d90e0ca3c3f` is retained at `/Users/rohtolos/Documents/MonoSync-Deskflow-deliverables/Windows/MonoSync-Windows-Portable.previous-036c8f301d9c.zip` for rollback.
- A concise handoff is at `/Users/rohtolos/Documents/MonoSync-Deskflow-deliverables/Windows/START-HERE.txt`; repo instructions are in `docs/WINDOWS-PORTABLE.md`. Windows network/firewall policy may still require administrator approval.
- GitHub run: `https://github.com/mahammadosmanov/mono-sync/actions/runs/40731756390`, source revision `485092eaf2a708391425f63fe6cd5b94aeb11e97`. Windows artifact ID `12834854969`; macOS artifact ID `12833919872`; neither was expired at last check. The original Windows CI artifact wraps both the MSI and portable ZIP; the standalone portable deliverable above avoids that ambiguity. Windows archive SHA-256 is `7804532b60db60e47038145d65f4d4e2828abec1a50c8e27ab6fb8ce14fa874d`; macOS archive SHA-256 is `b66afcfaf78029df735f507d63f22d4567abb33aa8e78d15d4d1f2e4b3e737d4`.
- The earlier workflow only uploaded the portable ZIP; the current workflow change replaces that default artifact with `MonoSync-Setup.exe` after per-user setup and lifecycle checks.
- Current Windows hardening removes service/elevation/update behavior from the Windows GUI path even with stale settings or without the portable marker. Windows settings/TLS use the current user's profile. Run `40816574201` exposed a generic settings-test expectation conflict; commit `7b5240c` updated that expectation and disabled-updater default. Run `40817892346` then passed Windows 28/28 and macOS 29/29.
- Corrected macOS artifact came from the same pinned source revision. Its ARM64 app bundle reports `1.27.0.0`. The downloaded CI bundle needed deep signing with the existing Apple Development identity because nested Qt signatures had a Team ID mismatch; deep strict verification then passed. The locally installed `/Applications/Deskflow.app` passes deep strict signature verification; executable SHA-256 is `617de79427203f6e3689bde2f27287d4feb337e6af9cecfe9c87da3ddec234dc`, and `--version` reports `Deskflow: 1.27.0`.
- Deskflow's main window was visibly confirmed in CUA. System Settings > Device Control and Data Access lists Deskflow with access `off`. The user authorized this exact permission, but macOS requires Touch ID or the account password to apply it; the OS authentication sheet is open for the user. Client mode is selected; the server address is blank and Connect remains disabled. No sharing session or peer trust was configured. Previous Mono Sync app copies were moved recoverably to `~/.Trash`; configuration remains preserved.

## Acceptance gate

On the actual Windows host and Mac client, verify 15-minute stability and 50+ edge crossings; exact mixed 200-character typing plus modifiers/function keys; pointer click/scroll and control hotkey; held-key/button release after network disconnect; and clean reconnect. Record OS versions, monitor layout/scaling, and logs. Build/unit tests cannot replace this gate. Do not start file transfer until the user accepts input behavior.

## Latest verified Windows user-mode build

- Source revision: `7b5240c14226218ad1131e0b6ba944b0e1b4d231` on `codex/deskflow-rebuild`.
- GitHub Actions run `40817892346` completed successfully: Windows CTest 28/28, macOS CTest 29/29, both packages built. Run URL: `https://github.com/mahammadosmanov/mono-sync/actions/runs/40817892346`.
- Windows artifact ID `12873447693`; outer artifact SHA-256 `a957b88ebacc45ee13ea6334e5fe83fb1c4a5702834a0de96cc36a6d8906cae6`, exact API digest match. Its one inner portable ZIP passes CRC and has SHA-256 `2ec62211f7f1c5966bfdacc554a48036b51a609c5c1f48bfb5820c6c861b9581`.
- Deliverable: `/Users/rohtolos/Documents/MonoSync-Deskflow-deliverables/Windows/MonoSync-Windows-Portable.zip`. Previous friendly ZIP is preserved at `/Users/rohtolos/Documents/MonoSync-Deskflow-deliverables/Windows/MonoSync-Windows-Portable.previous-036c8f301d9c.zip`.
- Inspected archive: 88 entries, includes Deskflow GUI/core, Qt/OpenSSL/MSVC runtime DLLs and license files, no daemon executable. Parsed embedded PE manifests: `deskflow.exe` and `deskflow-core.exe` use `asInvoker`, `uiAccess=false`.
- This confirms build/package policy and automated tests only. It does not confirm physical Windows runtime/input behavior. Windows Firewall or organization policy may still require administrator approval; macOS input-control authorization remains an OS-level gate. Do not start file-transfer work until the user accepts actual input behavior.

## Per-user Windows Setup

- User requirement: make `MonoSync-Setup.exe` the normal Windows download and keep installation, update, and removal available to a standard Windows account.
- `deploy/windows/standard-user-setup.nsi` builds a no-admin NSIS setup fixed at `%LOCALAPPDATA%\Programs\Mono Sync`. It writes Start menu shortcuts and uninstall metadata only in the current user's profile/registry. It does not add services, machine-wide firewall rules, or auto-start entries.
- `deploy/windows/package-standard-user-setup.py` wraps the verified portable ZIP, validates required app-local runtimes/licenses, removes the portable settings marker so profile/TLS data stays under `%APPDATA%\Deskflow`, and generates exact-file uninstall commands. Uninstall preserves profile contents and unrecognized files.
- `deploy/windows/verify-windows-as-invoker.py` checks PE manifests. Local NSIS 3.13 build passed for Setup.exe, the extracted uninstaller, `deskflow.exe`, and `deskflow-core.exe`; each is `asInvoker`, `uiAccess=false`.
- Local deliverable: `/Users/rohtolos/Documents/MonoSync-Deskflow-deliverables/Windows/MonoSync-Setup.exe`, SHA-256 `de44a23af2e9a3b0d1ca92f4942433a3b84aeda95a8a846ffe89e809c179947e`. It was built from the verified Windows payload in CI run `40817892346`, source revision `7b5240c14226218ad1131e0b6ba944b0e1b4d231`; this local wrapper build is not yet reproduced by the new CI workflow.
- CI now builds the setup by default and includes a filtered `runas /trustlevel:0x20000` install/update/uninstall smoke test that checks per-user profile preservation. Pending a Windows CI run; local macOS cannot execute NSIS output.
- Firewall policy and SmartScreen remain Windows OS/organization controls; no attempt is made to bypass them. Physical input acceptance is still pending.

## Next

Push and verify the workflow's Windows NSIS setup build and restricted-token install/update/uninstall smoke test. Then run the actual Windows host/Mac client input acceptance gate. No cross-device input acceptance has been recorded yet.
