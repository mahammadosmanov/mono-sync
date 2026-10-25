# 0011. File transfer on dedicated unidirectional streams

- Status: Accepted
- Date: 2026-10-25

## Context
Large transfers must not delay keystrokes or mouse motion. Files must be
verified and must not be able to write outside the download folder (T7).

## Decision
One QUIC unidirectional stream per file: header (version, name, size), data in
256 KiB chunks, SHA-256 trailer. The receiver writes to `<name>.part`, checks
the hash, then renames; mismatches are deleted. Names are sanitized to a bare
file name, existing files are never overwritten (`name (1).ext`).

## Alternatives considered
- Chunks on the control stream: head-of-line blocks input events.
- HTTP side channel: second port, second auth.

## Consequences
Input stays responsive during transfers (QUIC stream independence; to be
confirmed by the M5 latency benchmark). No resume yet: an interrupted file is
discarded and must be re-sent. Folder/drag-and-drop transfer is later work.
