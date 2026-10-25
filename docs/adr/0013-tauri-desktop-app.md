# 0013. Tauri desktop app over a shared core crate

- Status: Accepted
- Date: 2026-10-25
- Supersedes: 0012

## Context
The browser-tab control panel (ADR-0012) works but is not a real desktop app:
no window of its own, no tray icon, a console window on Windows, and no native
file drag-and-drop paths. The owner asked for a proper desktop app.

## Decision
- All logic (settings, identity, host/guest loops, file transfer, status) lives
  in `mono-core`. It has no UI code.
- `mono-app` is a Tauri 2 shell: one window, a tray icon (closing the window
  keeps sharing alive), single-instance, native file dialog and OS drag-and-drop
  that yields real file paths. The UI is plain HTML/JS in `crates/app/ui`, no
  Node toolchain or bundler. It talks to Rust only through five commands
  (`view`, `save`, `send_files`, `open_folder`, `quit`).
- `mono-cli` stays as a headless shell over the same core.
- Windows ships an NSIS installer (per user, no admin) and a portable exe;
  macOS ships a dmg. Both are unsigned for now.

## Alternatives considered
- Keep the browser panel: rejected by the owner.
- egui/iced: no OS drag-and-drop with paths on every platform without extra work.
- Electron: far larger runtime.

## Consequences
Needs the WebView2 runtime on Windows (preinstalled on Windows 11 and current
Windows 10). Linux CI needs webkit2gtk packages; Linux unmaintained transitive
crates are allow-listed in `deny.toml` with reasons. The local HTTP server and
its CSRF defences (T10) no longer exist, which removes that attack surface.
Installer size is larger than the bare binary.
