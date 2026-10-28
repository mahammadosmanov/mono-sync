# Current project state

Updated: 2026-10-28
RPS protocol: `.rps/PROTOCOL.md` (starter 0.1.0)
Stage: Alpha; local Mac install complete, Windows Setup.exe built
Profile: P2 provisional (interactive input and file transfer are user-facing)

## Product and current objective

`mono-sync` shares keyboard/mouse, text clipboard, and files between computers
on one LAN. Current reported issue: Windows host pointer movement is erratic
and keyboard input arrives late; file operations work. Exact OS versions,
display scaling, and reproduction steps are not yet recorded.

## Canonical document map

- Workspace/configuration: `Cargo.toml`, crate manifests, `.github/workflows/`
- Product: `docs/prd.md`, `README.md`
- Architecture: `docs/architecture.md`
- Decisions: `docs/adr/README.md` and `docs/adr/`
- Verification: `docs/testing.md`
- Change history: `CHANGELOG.md`

## Important constraints and decisions

- LAN-only scope; no accounts or cloud (`README.md`, `docs/prd.md`).
- Physical input is captured by OS hooks; avoid launching a configured instance
  without isolating its saved role and network connection.
- Multi-monitor and DPI acceptance is explicitly not complete (`README.md`,
  `docs/roadmap.md`, `docs/testing.md`).
- Windows input coordinate path uses `GetSystemMetrics` for screen size,
  low-level hook screen coordinates, and `SetCursorPos` for warps/injection.
  The repository does not set a Windows DPI-awareness manifest/config; actual
  process awareness and coordinate agreement on the affected Windows machine
  remain unverified (`vendor/rdev/src/windows/display.rs`,
  `vendor/rdev/src/windows/common.rs`, `vendor/rdev/src/windows/simulate.rs`).
- macOS input uses `CGDisplayBounds` and `CGEvent` global coordinates; the
  source change is consistent for single-display use. Multi-monitor layouts
  remain out of scope.

## Active work

- Change: investigate and fix erratic Windows host pointer movement and delayed
  keyboard input while preserving working file operations.
- Workspace/branch/commit: `/Users/rohtolos/Documents/GitHub/mono-sync`,
  `codex/windows-input-installer`, source commit
  `16f6b920f6214f47188a8ab19c5c280cb8189b2d`.
- Implementation status: input queueing, stale-event generation checks,
  held-input handling, and hotkey recognition are committed and pushed on the
  task branch. The Windows installer is an Actions artifact; no public release
  was created.

## Verification evidence

On macOS at the recorded commit:

- `cargo fmt --check` — passed.
- `cargo test --workspace` — passed (78 tests including loopback integration).
- `cargo build --workspace` — passed.
- `cargo clippy --workspace -- -D warnings` — passed; dependency `block v0.1.6`
  emitted a future-incompatibility warning.
- `cargo clippy -D warnings` — failed because Cargo requires lint flags after
  `--`; the corrected command above passed.

These checks do not verify real Windows OS input or cursor behavior. No
Windows-host reproduction has been run here.

On source commit `16f6b920f6214f47188a8ab19c5c280cb8189b2d` (2026-10-28):

- `cargo fmt --check` — passed.
- `cargo test --workspace` — passed, 93 tests across workspace crates,
  including loopback transfer and the new input/activation regressions.
- `cargo clippy --workspace --all-targets -- -D warnings` — passed. Cargo still
  reports the existing future-incompatibility warning for `block v0.1.6`.
- `cargo build --workspace` — passed.
- `git diff --check` — passed.
- `CARGO_TARGET_DIR=/tmp/mono-sync-input-windows-target cargo check -p mono-input --target x86_64-pc-windows-msvc` — passed on the macOS host; this checks the input crate for Windows, not the desktop app or runtime behavior.
- From `crates/app`, `APPLE_SIGNING_IDENTITY="Apple Development: rohtolos@gmail.com (87V22Z9YD7)" cargo tauri build --bundles app` — passed and created a signed arm64 v0.1.0 app at `target/release/bundle/macos/mono-sync.app`. Tauri skipped notarization because notarization credentials are not configured; it also warned that the existing bundle identifier ends in `.app`.
- `codesign --verify --deep --strict --verbose=2 target/release/bundle/macos/mono-sync.app` — passed.
- Installed `/Applications/mono-sync.app` from that bundle after making a recoverable copy. Its executable SHA-256 matches the built bundle executable: `0791f6a991164c47e28194bb054072f6017b9b2924cb680724e7462bb95c4265`. Bundle identifier remains `dev.monosync.app`; installed signature verification passed.
- Original signed app was copied to `/Users/rohtolos/Documents/mono-sync-app-backups/mono-sync.app-20261028T062318Z` and retained at `/Applications/.mono-sync.app.previous-20261028T062318Z`. Both original executable copies match SHA-256 `79da4961522713c2f32232e3d3f6ec09ead2f859a75d2b34878ae3cb693b3802`.
- The app was not launched after installation. Its saved configuration uses guest role and an existing host/pairing configuration, so launching could immediately connect and begin input sharing. User config was not modified.
- Windows NSIS installer built from source commit `16f6b920f6214f47188a8ab19c5c280cb8189b2d` by workflow run [40645785170](https://github.com/mahammadosmanov/mono-sync/actions/runs/40645785170). Both matrix jobs passed; the Windows job completed build, collection, and upload. Artifact ID `12798072524`; uploaded ZIP SHA-256 `cdcce45af4219c6808833de1cf007aafb2267994e31b2e13785b44f2ed07ade9`. Workflow dispatch used a branch ref, so release attachment was skipped.
- Downloaded Windows artifacts to `/Users/rohtolos/Documents/mono-sync-windows-installer-20261028/`. Setup installer: `mono-sync_0.1.0_x64-setup.exe`, 3,112,619 bytes, SHA-256 `5d84b87ba73e22f59d5ca6adcaf8fa98b6733fa12fb7f02ccfb7464c282af8b5`. Portable binary: `mono-sync-portable-windows-x64.exe`, 13,137,920 bytes, SHA-256 `cad4fd43a77826a56d0e6405966885998bbb1506537ebfba251f1c73c2548aae`.
- `file` identifies the setup as a Nullsoft installer and the portable executable as PE32+ x86-64 for Windows. The release workflow has no Authenticode signing step; installer is unsigned. Installer was not executed on macOS. Windows installation/runtime behavior and manual cross-OS input acceptance remain unverified.
- The task branch is pushed to `origin`. The existing workflow was sufficient; no workflow source edit or public release was needed. A previous source ZIP also exists at `/Users/rohtolos/Documents/GitHub/mono-sync-windows-handoff-20261028-final.zip`, but the Windows installer artifact above is the requested deliverable.

## Blockers / unknowns / risks

- Need Windows host and guest OS versions, scaling settings, display layout,
  exact cursor crossing action, and app log lines from the failing attempt.
- Windows DPI coordinate assumptions need Windows-runtime confirmation.
- Queue latency and OS-hook behavior need measurement on the affected Windows
  machine; static inspection alone cannot establish a runtime cause.
- The queue now merges only same-direction movement per axis, preserving the
  guest's per-message boundary clamping, and generation checks reject stale
  queued steps during connection activation.
- A stale event invalidated during activation retains the prior swallow
  behavior; a physical input transition may therefore be dropped at that narrow
  reconnect boundary. This avoids forwarding a stale remote keydown to the new
  guest. It still needs Windows hardware acceptance.
- The Windows installer is unsigned and has not been installed on a Windows
  machine. Windows hardware input acceptance remains necessary.
- A local macOS install and bundle executable match byte-for-byte, but that
  package check says nothing about the Windows executable or user symptom.

## Next safe action

Install the Windows Setup.exe on the affected Windows host. Record OS versions,
display scaling/resolution, and the relevant mono-sync log around one cursor
crossing and a short typed sequence. Confirm pointer movement and keyboard
latency on hardware; a successful build does not establish runtime acceptance.
