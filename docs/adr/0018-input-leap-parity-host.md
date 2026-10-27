# 0018. Host side follows Input Leap's primary-side rules

- Status: Accepted
- Date: 2026-10-27

## Context
ADR-0017 stopped the bounce with a 4 px hold and a 3 px stale-edge filter. Both
drop real motion: slow host motion while remote is lost, and the thresholds were
tuned to one machine's jitter. Input Leap is a mature tool with the same
parked-centre design on Windows. It forwards every move and has no hold
threshold.

Input Leap is GPL-2.0. We read its source (github.com/input-leap/input-leap,
commit 34a34fb) for behaviour. These are the five facts this design relies on:

1. `MSWindowsScreen::onMouseMove` takes the delta from the last saved position.
   A zero delta returns early. Any other delta is kept.
2. On a secondary screen the host warps to the centre on each report. The warp's
   own echo is discarded by PRE_WARP/POST_WARP message ordering
   (`MSWindowsScreen::onPreDispatchPrimary`), not by position.
3. The bogus zone: `bogusZoneSize = 10`. A delta is dropped when it would take the
   centred cursor to within 10 px of a screen edge.
4. `Server::onMouseMoveSecondary` adds the delta to a virtual cursor. It returns
   control when that cursor leaves the peer's screen through the back edge, and
   clamps otherwise.
5. `MSWindowsScreen::leave` warps the host cursor to the centre. `PrimaryClient::enter`
   puts it back at the exit point. `Server::avoidJumpZone` insets only when the
   destination is the primary, so the peer is entered on its exact edge.

## Decision
While a peer owns the cursor:

- (a) Every pointer event is swallowed: moves, buttons and wheel, and the crossing
  and return moves too. Keys are forwarded and swallowed.
- (b) Every non-zero offset from the parked centre is forwarded, and the peer
  accumulates it. There is no hold threshold. A zero offset is not forwarded.
- (c) An offset that would take the centred cursor to within 10 px of a screen
  edge is bogus and is dropped. Every other report re-parks the cursor at the
  centre. A report exactly at the centre is the echo of one warp, or a zero move:
  it is dropped without a warp. One echo is awaited per warp, capped at 8. An
  echo must match the warp target exactly; there is no tolerance band.
- (d) Moving past the peer's back edge returns control. The host cursor is placed
  on its own edge, 2 px in, not at the centre. Reports within 30 px of that edge do
  not cross until the cursor has moved 30 px away (ADR-0017 hysteresis, kept).
- (e) On crossing, the host warps to the centre before `Enter` is sent
  (`os.rs::apply` now warps first). The crossing move is swallowed.

The per-warp trace lines (first 8 pointer events after a crossing) and the
readback window (ADR-0017 performance change) are kept.

## Licensing
Input Leap is GPL-2.0. We read its source to learn its behaviour and copied none
of its code. `crates/input` is MIT OR Apache-2.0, and the code here was written
from this record. The rules describe behaviour, not expression.

## Alternatives considered
- Keep the 4 px hold and the 3 px filter (ADR-0017). Rejected: they drop real
  motion and were tuned to one machine's jitter.
- Sentinel-based echo matching, as Input Leap does. Rejected for now:
  `rdev::grab` gives us no message loop around the hook. An exact-position counter
  is the closest we can build.
- A tolerance band for echoes. Rejected by the requirement: exact match only.

## Consequences
- Slow motion while remote is no longer lost.
- Jitter is now real motion. ADR-0017 recorded 1-3 px of jitter on Windows that,
  at entry, counted as an exit and bounced the cursor back about four times. These
  rules admit that again: a single pixel toward the back edge while the peer's
  cursor sits on that edge returns control. Input Leap has the same property
  (point 5). The first Windows run must check it.
- A warp that lands off its target is not an echo. It is forwarded and re-parked.
  A constant landing error therefore moves the peer by that error on every warp.
  The warp log shows the landing.
- A stale report between a warp and its echo is dropped only within 10 px of the
  edge. A stale report farther in is forwarded as motion. Input Leap prevents this
  with sentinels we do not have.
- Tested with unit tests on macOS and a Windows target compile. Not run on Windows
  and not run with hardware.
- ADR-0017's hold and stale-edge decisions are superseded by this record. Its
  hysteresis, edge placement and trace stay.
