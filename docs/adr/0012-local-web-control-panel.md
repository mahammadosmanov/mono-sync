# 0012. Local web control panel instead of a native GUI toolkit

- Status: Superseded by 0013
- Date: 2026-10-25

## Context
Users need to pick a role, direction, paste a pairing code, send files and
see status, without a terminal. The app must stay a single binary that installs
without admin rights (NFR-6, NFR-7) and keep idle memory low (NFR-3).

## Decision
`mono` serves one HTML page on `127.0.0.1` (port 24801, else a free port) and
opens it in the default browser. The page talks to a small JSON API in the same
process. Every request needs a random per-run key (in the opened URL, sent as
`X-Mono-Key`) and a loopback `Host` header, which blocks cross-site requests
and DNS rebinding (threat T10). Files are dropped onto the page and streamed
to the process.

## Alternatives considered
- Tauri/WebView: native window and tray, but needs WebView2/Xcode toolchains,
  larger binary, more CI complexity.
- egui/iced: pure Rust window, but no file drag-and-drop from the OS on all
  platforms without extra work and a heavier dependency tree.
- CLI only: fails the "non-technical user" goal.

## Consequences
Zero extra dependencies beyond `tiny_http`. The UI lives in a browser tab, so
there is no tray icon and the console window stays open on Windows. Revisit
with a tray plus native window (Tauri) when polishing for 1.0, or if browsers
restrict localhost access.
