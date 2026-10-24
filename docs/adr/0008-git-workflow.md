# 0008. Git workflow and commit conventions

- Status: Accepted
- Date: 2026-10-24

## Context
We want a stable `main`, reviewable history and automatic changelogs.

## Decision
- `main` is always releasable. All work happens on short-lived branches:
  `feat/…`, `fix/…`, `docs/…`, `chore/…`, `ci/…`, `refactor/…`, `test/…`.
- A branch merges to `main` only when fmt, clippy (warnings as errors) and
  tests pass, using a merge commit (`--no-ff`) so each feature is one visible unit.
- Commits follow Conventional Commits (`type(scope): summary`).
- Releases are tagged `vX.Y.Z` (SemVer) and recorded in `CHANGELOG.md`.
- Pushes to the remote and release tags are deliberate maintainer actions.

## Alternatives considered
- Squash merges: lose useful intermediate commits.
- Git flow with long-lived `develop`: overhead for a small team.

## Consequences
Readable history; changelog and bisecting stay easy.
