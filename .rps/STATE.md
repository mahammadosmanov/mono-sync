# Current project state

Updated: 2026-10-28
RPS protocol: `.rps/PROTOCOL.md` (starter 0.1.0)
Stage: Alpha; local Mac installation complete, Windows handoff prepared
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
  `main`, `bd6670a539c8d7706132b8b7d4c09a58c06312a1`.
- Implementation status: local source changes for input queueing, stale-event
  generation checks, held-input handling, and hotkey recognition are in the
  shared working tree. Not committed or released.
- Uncommitted files: `.rps/` adoption docs and changes in
  `crates/core/src/run.rs` plus `crates/input/`. Recheck `git status` before
  editing.

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

On the current uncommitted shared diff (2026-10-28):

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
- Windows source handoff archive: `/Users/rohtolos/Documents/GitHub/mono-sync-windows-handoff-20261028-final.zip`. It includes current uncommitted changes and `.rps` docs; `.git`, `target`, `node_modules`, environment files, key files, and external user config are excluded. The archive includes the native Windows build instructions and the workflow’s `cargo tauri build --bundles nsis` command.
- This Mac has no PowerShell or Windows runner. The repository workflow uses `windows-latest`; only `mono-input` passed a Windows-target `cargo check` from macOS. No Windows desktop binary/installer or runtime/hardware validation was produced. Native Windows build and the manual cross-OS checklist remain required.

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
- The local Tauri bundle is macOS-only. No Windows installer/executable was
  produced; the release workflow builds Windows on `windows-latest`.
- A local macOS install and bundle executable match byte-for-byte, but that
  package check says nothing about the Windows executable or user symptom.

## Next safe action

Validate the updated Windows executable on the affected host/guest pair. Record
OS versions, display scaling/resolution, and the relevant mono-sync log around
one cursor crossing and a short typed sequence. Confirm the reported movement
and keyboard latency are resolved on hardware; the local tests and Windows
crate check do not establish that acceptance.
