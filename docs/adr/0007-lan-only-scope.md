# 0007. LAN-only scope

- Status: Accepted
- Date: 2026-10-24

## Context
Internet access needs relays, NAT traversal and accounts, which conflict with
low latency, privacy and zero running cost.

## Decision
Peers must reach each other directly on the same network. No project-run
servers, no telemetry, no outbound traffic except to paired peers.

## Alternatives considered
- Relay/cloud mode: costs money, adds latency, enlarges attack surface.

## Consequences
Simple trust model and zero ops. Does not work across networks or VPN-less
remote use; users can bring their own VPN.
