# 0003. QUIC transport with datagrams for motion

- Status: Accepted
- Date: 2026-10-24

## Context
Input needs two delivery modes. Key and button events must be ordered and
reliable (a lost key-up sticks a key). Mouse motion is high-rate and stale
data is useless: retransmitting old positions adds lag. We also need
encryption, and later file transfer over the same link.

## Decision
Use QUIC (`quinn`, rustls with ring). Ordered messages on a bidirectional
stream; `Move` as unreliable QUIC datagrams. File transfer later uses extra
streams on the same connection.

## Alternatives considered
- TCP+TLS only: head-of-line blocking delays fresh motion behind lost packets.
- Raw UDP + custom reliability and crypto: more code, more security risk.
- WebRTC data channels: heavy dependency, complex signalling for a LAN tool.

## Consequences
One encrypted connection, two delivery classes, built-in keepalive and
multiplexing. QUIC uses UDP, which some corporate firewalls block; TCP
fallback is an open question (PRD Q1).
