# 0004. Trust model: pinned fingerprint plus pairing token

- Status: Accepted
- Date: 2026-10-24

## Context
Whoever can send input to a guest controls it; whoever receives a host's
stream sees its keystrokes, including passwords. The LAN is not trusted.

## Decision
- Host has a self-signed certificate. A guest connects only if the host's
  certificate SHA-256 fingerprint equals the pinned value (protects the guest
  from an impostor host).
- The guest sends a pairing token in its first message; the host drops the
  connection unless it matches in constant time (protects the host's input
  from strangers).
- Fingerprint and token are shared out-of-band as one pairing code.
- Persistent identity and one-time pairing flow arrive in M2.

## Alternatives considered
- Trust on first use without a code: vulnerable to active LAN attacker at pairing.
- Public CA certificates: impossible for LAN hosts without domains.
- Mutual TLS with guest certs: stronger long-term; planned as the M2 outcome
  of pairing (token exchanges guest cert), without changing this model.

## Consequences
No accounts, no servers. Users must transfer a short code once. The token in
M1 is static and must be treated as a password.
