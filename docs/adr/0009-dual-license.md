# 0009. Dual license MIT OR Apache-2.0

- Status: Accepted
- Date: 2026-10-24

## Context
We want maximum adoption and a patent grant, matching the Rust ecosystem norm.

## Decision
License all project code under MIT OR Apache-2.0 (user's choice). Contributions
are accepted under the same terms. Dependencies must have compatible licenses;
GPL code must not be copied in.

## Alternatives considered
- GPL (as Barrier/Deskflow): blocks some reuse; not required here.
- MIT only: no explicit patent grant.

## Consequences
Dependency licenses are checked in CI (`cargo-deny`, added with the CI branch).
