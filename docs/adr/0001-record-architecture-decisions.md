# 0001. Record architecture decisions

- Status: Accepted
- Date: 2026-10-24

## Context
The project spans three OS families and a network protocol. Reasons behind
choices get lost, and contributors re-open settled questions.

## Decision
Keep short ADRs in `docs/adr/`, one file per decision, numbered, immutable
once accepted.

## Alternatives considered
- Wiki or issues: drift from the code, not reviewed with it.
- No records: loses the reasoning.

## Consequences
Decisions are reviewed in pull requests. Small overhead per decision.
