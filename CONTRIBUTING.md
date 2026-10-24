# Contributing to mono-sync

Thanks for helping. This file is the short version; decisions live in `docs/`.

## Setup

- Install Rust (stable) via <https://rustup.rs>.
- Linux only: `sudo apt-get install libx11-dev libxi-dev libxtst-dev`.
- macOS: the binary that captures or injects input needs **Accessibility** and
  **Input Monitoring** permission (System Settings → Privacy & Security).

```sh
cargo build --workspace
cargo test --workspace
```

## Workflow (ADR-0008)

1. Branch from `main`: `feat/…`, `fix/…`, `docs/…`, `chore/…`, `ci/…`, `refactor/…`, `test/…`.
2. Commit with [Conventional Commits](https://www.conventionalcommits.org):
   `feat(net): add reconnect backoff`.
3. Before opening a pull request, all of these must pass:
   ```sh
   cargo fmt --all --check
   cargo clippy --workspace --all-targets -- -D warnings
   cargo test --workspace
   cargo deny check
   ```
4. Open a PR against `main` using the template. Keep it to one concern.
5. Changes to costly-to-reverse decisions need a new ADR (`docs/adr/`).
6. User-visible changes need a `CHANGELOG.md` entry under *Unreleased*.

## Code rules

- No OS calls outside `mono-input` backends. Protocol and layout logic stay pure.
- Every new non-trivial logic has a test. OS-input behaviour that CI cannot
  assert goes in the manual checklist (`docs/testing.md`).
- `unsafe` needs a `// SAFETY:` comment.
- Never log keystrokes, tokens or clipboard contents.

## Licensing

By contributing you agree your work is licensed under MIT OR Apache-2.0 (ADR-0009).
