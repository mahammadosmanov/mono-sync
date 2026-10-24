# 0005. rdev for input capture and injection

- Status: Accepted (revisit criteria below)
- Date: 2026-10-24

## Context
Capturing, swallowing and injecting input needs OS-specific code on Windows
(low-level hooks, SendInput) and macOS (CGEventTap, CGEventPost). Writing both
first delays the first usable trial.

## Decision
Use the `rdev` crate behind our own `Capture` / `Inject` traits in
`mono-input`, so it can be replaced per OS without touching other crates.

## Alternatives considered
- Native backends from day one: best control, slowest start.
- `enigo` for injection only: no capture/grab.

## Consequences
Fast start. Replace the backend on an OS when any of these hold: relative
motion cannot be obtained or is wrong at screen edges; swallowing of events
is unreliable; latency budget (NFR-1) cannot be met; the crate is unmaintained
for a supported OS release.
