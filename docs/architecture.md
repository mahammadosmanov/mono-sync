# Architecture

## Overview

```
 HOST (owns keyboard + mouse)                 GUEST (receives input)
┌──────────────────────────────┐            ┌──────────────────────────────┐
│ input capture (OS hooks)     │            │ input injection (OS APIs)    │
│        │                     │            │        ▲                     │
│ edge/layout state machine    │  QUIC/TLS  │ replay + release-all guard   │
│        │                     │ ◄────────► │        ▲                     │
│ session (mono-net)           │  stream:   │ session (mono-net)           │
│                              │  keys,btns │                              │
│                              │  datagram: │                              │
│                              │  motion    │                              │
└──────────────────────────────┘            └──────────────────────────────┘
```

A machine can be both host and guest in the same layout (each owns its own
input when the cursor is on its screens). In M1 roles are fixed per connection.

## Crates

| Crate | Responsibility | Depends on |
|---|---|---|
| `mono-proto` | Wire messages and encoding. No I/O. | serde, postcard, rdev (types only) |
| `mono-net` | QUIC endpoints, pairing handshake, session. | mono-proto, quinn, rustls |
| `mono-input` (M1) | `Capture` and `Inject` traits, per-OS backends, edge logic. | mono-proto |
| `mono-cli` (M1) | `mono host`, `mono guest`. | all |
| `mono-app` (M4) | Tray UI. | all |

Rule: the protocol and the layout logic contain no OS calls, so they are unit
testable on any machine. OS code lives only behind the `mono-input` traits.

## Data flow, cursor crossing

1. Host captures events. While the cursor is on a host screen, events pass
   through to the host OS untouched.
2. Cursor reaches a layout edge linked to a guest. Host sends `Enter{x,y}`,
   hides/locks its local cursor and starts swallowing local input.
3. Host sends `Move`, `Button`, `Key`, `Wheel` to the guest. Motion is sent as
   QUIC datagrams (lossy, newest wins). Everything else uses the ordered stream.
4. Cursor returns across the guest edge: guest sends `Leave`; host resumes.
5. On disconnect, timeout or panic the guest releases every key and button it
   believes is down (FR-6).

## Protocol

Messages are `postcard`-encoded enums (`mono_proto::Msg`). Stream frames are
`u32` big-endian length + payload, max 64 KiB. Datagrams carry one message.
The first stream message is `Hello{version, token}`; the host answers
`HelloOk` or closes the connection. A version bump is required for any change
to the encoding of existing variants; new variants are appended.

## Security

See `docs/threat-model.md` and ADR-0004.

## Testing strategy

- Unit: protocol round trips, layout/edge state machine.
- Integration: loopback QUIC sessions, auth failures.
- Manual cross-OS checklist per milestone (`docs/testing.md`, added with M1),
  because OS input injection cannot be asserted in headless CI.
- Benchmarks: latency and throughput harness added in M5.
