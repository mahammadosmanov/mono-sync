# 0010. Read remote motion as offset from a parked centre cursor

- Status: Accepted (revisit criteria below)
- Date: 2026-10-24

## Context
While a peer owns the cursor the host must keep reading mouse motion, but the
OS clamps the cursor at screen edges, so absolute positions stop changing.
`rdev` reports absolute positions only, not raw deltas (ADR-0005).

## Decision
While remote, park the local cursor at the screen centre. Motion is the offset
of each reported position from the centre, followed by a warp back to the
centre. The echo event of our own warp is swallowed. Keys, buttons and wheel
are swallowed and forwarded. The logic is pure (`mono_input::host`) and unit tested.

## Alternatives considered
- Raw deltas via CGEventTap (macOS) and Raw Input (Windows): exact, but two
  native backends before the first trial.
- Swallow moves and trust event positions: unverified; positions may stop
  updating when the cursor does not move.

## Consequences
Works with only absolute positions. The local cursor jitters near the centre
while remote, and apps under it may see hover events. Revisit with native raw
delta backends if: NFR-1 latency is missed, motion drifts or stalls in the
manual checklist, or warp echo handling proves unreliable on either OS.
