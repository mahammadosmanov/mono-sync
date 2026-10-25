# Threat model

Scope: two or more machines on one LAN running mono-sync. Assets: control of
a machine's input, confidentiality of keystrokes/clipboard/files, integrity
of transferred files.

## Attackers

| Attacker | Capability |
|---|---|
| A1 passive LAN observer | Reads and records packets. |
| A2 active LAN attacker | Connects to ports, spoofs hosts, replays or modifies packets. |
| A3 malicious local user/app | Runs unprivileged code on an endpoint. |

## Threats and mitigations

| # | Threat | Mitigation | Status |
|---|---|---|---|
| T1 | A1 reads keystrokes | QUIC/TLS 1.3 encryption | Done (M0) |
| T2 | A2 impersonates a host to inject input into a guest | Guest pins host cert fingerprint (ADR-0004) | Done (M0) |
| T3 | A2 connects as a guest to receive a host's keystrokes | Pairing token, constant-time check, 5 s handshake timeouts | Done (M0) |
| T4 | Token guessing | Long random token (generation in M1 CLI); persistent pairing with per-guest certs in M2 | Planned |
| T5 | Oversized/malformed frames exhaust memory | 64 KiB frame cap; decode errors drop the stream | Done (M0) |
| T6 | Stuck keys/buttons after disconnect cause unintended input | Release-all on every disconnect path (FR-6) | M1 |
| T7 | Path traversal or overwrite via file transfer | Sanitize names, write to a download dir only, no overwrite without consent | M3 |
| T8 | Clipboard leaks secrets to peers | Opt-in clipboard sync; never logged | M3 |
| T9 | A3 abuses injection permission | Rely on OS permissions; no listening on localhost without auth | Ongoing |
| T10 | (Retired) local control panel reachable from web pages | The browser panel was replaced by a Tauri window with in-process commands (ADR-0013); no local HTTP server remains | N/A |
| T11 | Identity key or pairing token read by another local user | Files in the per-user config dir written owner-only (0600 on Unix; Windows relies on the user profile ACL) | Done |

## Accepted limits

- A compromised paired machine can control its peers while paired.
- Static token in M1 is a password: anyone holding the pairing code can pair.
- Denial of service on the UDP port by a LAN attacker is not prevented.
