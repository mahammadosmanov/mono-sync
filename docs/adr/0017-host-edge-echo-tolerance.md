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
- While remote, a pointer report within 4 px of the centre on both axes is
  held: not forwarded, and the local cursor is not re-parked. The OS keeps the
  small offset, so real motion adds up and is forwarded once it passes 4 px.
  Re-parking on held jitter would start a chain of warp echoes, so it is skipped.
- A report within 2 px of the host screen edge while remote is a stale
  pre-warp report: dropped, no re-park.
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
- Sub-4 px motion is delayed until it adds up. It is not lost.
- The local cursor can sit up to 3 px off centre while remote (ADR-0010 already
  accepts local jitter near the centre).
- A single report 958 px or more from the centre is treated as stale. A hard
  flick could in theory do that; it has not been seen.
- A constant parking error from the OS stays inside the 4 px hold and is not
  corrected. The per-crossing trace lines show whether it happens.
- Tested with unit tests on macOS only. Not run on Windows or with real
  hardware. The next Windows run's log lines are the check.
- Refines ADR-0010; its decision is unchanged.
