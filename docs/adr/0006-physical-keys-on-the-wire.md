# 0006. Physical key identities on the wire

- Status: Accepted
- Date: 2026-10-24

## Context
Sending characters breaks shortcuts and non-text keys; sending OS key codes
breaks across OSes. Cross-OS compatibility is a primary goal (PRD G2).

## Decision
Send named physical keys (`mono_proto::Key`, from rdev) plus down/up state.
The receiving OS interprets them with its own active layout. Modifier remap
profiles (Cmd↔Ctrl, Option↔Alt) are applied on the guest side in M4.

## Alternatives considered
- Characters/Unicode: loses non-printing keys and shortcuts.
- USB HID usage codes as our own enum: cleaner long-term; adopt if rdev's key
  set proves incomplete (would be a protocol version bump).

## Consequences
Layout differences between machines behave as users expect for shortcuts.
Keys unknown to rdev arrive as `Unknown(code)` and are not portable; they are
logged and dropped.
