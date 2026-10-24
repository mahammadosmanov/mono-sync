# Product Requirements Document: mono-sync

Status: Draft v0.1 · Owner: project maintainers · Last updated: 2026-10-24

## 1. Problem

People who work on two or more computers side by side (a Mac and a Windows
PC, two laptops, a desktop and a test machine) juggle several keyboards and
mice, and move files through USB sticks, chat apps or cloud drives. Existing
software-KVM tools work, but are either single-OS, closed, or weak at the two
things users notice most: input feel (latency, key mapping across OSes) and
file transfer.

## 2. Vision

Move the cursor off the edge of one screen and onto the next computer. Type
there. Drop a file there. It feels like one machine.

## 3. Goals

- G1. One keyboard and mouse control N computers on the same LAN by moving the
  cursor across screen edges.
- G2. Cross-OS compatibility is a first-class feature: a key pressed on one OS
  produces the same key on the other, including modifiers and layout quirks.
- G3. Input feels local: latency and jitter low enough that users do not notice.
- G4. Move files and clipboard content between machines with minimal steps.
- G5. Safe by default: only paired devices, encrypted traffic.
- G6. Open source, easy to build, easy to install, no accounts, no cloud.

## 4. Non-goals

- Internet or relay access, NAT traversal, remote desktop or screen sharing.
- Mobile platforms.
- Video/audio streaming.
- Cloud accounts, telemetry, or any server run by the project.
- Linux Wayland in v1 (Linux X11 is a later milestone).

## 5. Users and scenarios

| Persona | Scenario |
|---|---|
| Developer with Mac laptop and Windows desktop | Types code on one keyboard, tests on the other machine without swapping peripherals. |
| Office worker with two PCs | Drags a report from one machine to the other instead of emailing it. |
| QC / admin on a locked-down corporate PC | Installs without admin rights, runs on a poor network. |

## 6. Functional requirements

Priority: P0 = needed for first usable trial, P1 = needed for 1.0, P2 = later.

| ID | Requirement | Pri |
|---|---|---|
| FR-1 | Host shares its keyboard and mouse with one or more guests. | P0 |
| FR-2 | Cursor crosses screen edges according to a user-defined layout (left/right/top/bottom). | P0 |
| FR-3 | Keyboard events map by physical key; modifier mapping profiles (Cmd↔Ctrl, Option↔Alt). | P0 |
| FR-4 | Mouse buttons, relative motion and scrolling are forwarded. | P0 |
| FR-5 | Pairing with a one-time code; encrypted transport; pinned host identity. | P0 |
| FR-6 | Release all pressed keys/buttons on disconnect or focus loss (no stuck keys). | P0 |
| FR-7 | Hotkey to force control back to the host. | P0 |
| FR-8 | Clipboard text sync. | P1 |
| FR-9 | Send files to a connected machine (tray/CLI), with integrity check and resume. | P1 |
| FR-10 | Copy/paste of files across machines. | P1 |
| FR-11 | Drag and drop of files across the screen edge. | P2 |
| FR-12 | Automatic discovery of peers on the LAN (mDNS). | P1 |
| FR-13 | Tray app with status, layout editor and settings. | P1 |
| FR-14 | Per-screen DPI/scale handling and multi-monitor layouts. | P1 |
| FR-15 | Auto-start at login, auto-reconnect. | P1 |
| FR-16 | Three or more machines in one layout. | P1 |
| FR-17 | Linux X11 support. | P2 |

## 7. Non-functional requirements

These are **targets**, not measurements. Each is verified by a benchmark or a
test before the milestone that claims it closes (see `docs/roadmap.md`).

| ID | Requirement | Target |
|---|---|---|
| NFR-1 | Added input latency on wired/good Wi-Fi LAN, p99 | < 5 ms wired, < 15 ms Wi-Fi |
| NFR-2 | Host CPU while moving the mouse | < 3 % of one core |
| NFR-3 | Idle CPU and memory | < 0.5 % CPU, < 50 MB RSS |
| NFR-4 | Reconnect after network drop | < 3 s |
| NFR-5 | File transfer throughput | ≥ 80 % of measured link speed |
| NFR-6 | Install without admin rights on Windows | Required |
| NFR-7 | Single binary per OS, no runtime to install | Required |
| NFR-8 | No outbound connections other than to paired peers | Required |

## 8. Platform scope

- v1: Windows 10/11, macOS 13+.
- Later: Linux X11, then Wayland (libei) if feasible.
- Known limits, documented not hidden: UAC/secure-desktop and lock screens on
  Windows, login window on macOS, apps running elevated.

## 9. Success metrics

- A Mac and a Windows PC are usable as one workstation for a full workday
  without restart or stuck-key incident.
- p99 added latency meets NFR-1 in the published benchmark.
- A new user goes from download to working setup in under 5 minutes.

## 10. Risks

| Risk | Mitigation |
|---|---|
| OS input APIs differ and change (macOS permissions, Windows hooks) | Isolate behind a platform trait (ADR-0005); CI on both OSes. |
| `rdev` limits (relative motion, maintenance) | Exit criteria in ADR-0005; native backends if hit. |
| Unsigned binaries trigger Gatekeeper/SmartScreen | Document; sign when funding allows. |
| Keystroke leakage on LAN | Mandatory TLS + pairing (ADR-0004, `docs/threat-model.md`). |
| Corporate firewalls block UDP | Fixed port, documented rule; TCP fallback evaluated in M5. |
| Stuck keys after crash | FR-6 with release-all on every disconnect path; tested. |

## 11. Open questions

- Q1. TCP fallback for networks that block UDP: needed for v1?
- Q2. Drag-and-drop across the edge: feasible on both OSes without privileged APIs?
- Q3. Code signing budget and timing.
