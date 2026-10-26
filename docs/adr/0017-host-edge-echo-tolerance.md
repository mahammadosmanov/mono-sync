# 0017. Hold warp jitter and drop stale edge reports while remote

- Status: Accepted
- Date: 2026-10-26

## Context
While a peer owns the cursor, the host parks the local cursor at the screen
centre and reads motion as the offset from that centre (ADR-0010). On Windows
the parked cursor reports 1-3 px of jitter around the centre. On entry the
virtual cursor sits exactly on the peer's back edge (x = 0), so one pixel of
jitter toward that edge counts as a real exit. The host sent Leave, warped to
the other side, and a queued report at the old edge re-entered. A single
crossing bounced back and forth about four times ("moved", "came back").

Reports queued before the warp still show the old host edge (x = width-1 or
width-2). Read as offsets from the centre they are about 960 px of motion,
which moved the peer's cursor across its whole screen.

## Decision
- While remote, every pointer report except an exact-centre echo of our own warp is re-parked: the local cursor is warped back to the centre. A report within 4 px of the centre on both axes (jitter) is re-parked but not forwarded. Re-parking is no longer skipped for held reports (see Consequences).
- A report within 3 px of the host screen edge while remote is a stale pre-warp report: not forwarded, and re-parked like the others.
- The exit rule is unchanged (`layout.rs`): control returns only on forwarded
  motion past the peer's back edge. The first pixel of jitter no longer
  reaches that rule.
- An exact centre report stays a swallowed echo, as before.
- Diagnostics: the first 8 pointer events after each crossing log one line
  (raw position, offset, decision, warp) through `core`'s log.

## Alternatives considered
- Larger dead zone alone (for example 16 px): rejected. It delays every real
  exit by that much and does not remove stale edge reports.
- OS-level cursor clipping: rejected. The API differs per OS (Windows
  `ClipCursor`; macOS has no direct equivalent for our event tap), and clipping
  changes what the OS does with the pointer rather than how we read it.

## Consequences
- Returning to the host needs 4 px of real motion past the edge, not 1 px.
- Re-parking leaves no OS offset to add up. Sub-4 px motion while remote is therefore not forwarded and does not sum: a slow move of the host cursor there is lost, and only one report 4 px or more past the back edge returns control. Whether this is acceptable needs the first hardware run.
- The local cursor is re-parked on every report, so it is only off centre for the instant between a report and its warp. A constant warp error of 1-3 px is re-parked on every report and never corrected; the warp log shows it (`warp to (960,540) -> OS at (x,y)`).
- A single report 957 px or more from the centre is treated as stale. A hard flick could in theory do that; it has not been seen.
- Stale and held reports now re-park instead of being dropped, so the cursor cannot stay on the edge after a crossing. The first 8 warps after each crossing are logged with where the OS put the cursor; later failures are logged at most every 2 s (`warp failed: ...`). Windows reads each warp back with `GetCursorPos`; more than 2 px off is a failure.
- Tested with unit tests on macOS only, plus a Windows target compile. Not run on Windows or with real hardware. The next Windows run's log lines are the check.
- Refines ADR-0010; its decision is unchanged.
- Windows host: every pointer report while remote is swallowed, the crossing move included, as Input Leap's relay mode does. The OS never moves the physical cursor off the centre, so it cannot snap back to the edge.
