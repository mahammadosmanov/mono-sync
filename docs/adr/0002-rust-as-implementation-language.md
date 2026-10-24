# 0002. Rust as implementation language

- Status: Accepted
- Date: 2026-10-24

## Context
We need low and predictable latency, one codebase for Windows and macOS (Linux
later), single self-contained binaries, memory safety in a program that
handles every keystroke, and no runtime to install on locked-down PCs.

## Decision
Implement the core, CLI and (later) app in Rust, as a Cargo workspace.

## Alternatives considered
- Go: easy cross-compiling, but GC pauses and weaker OS-API ergonomics for hooks.
- C++: used by Synergy-family tools; memory-safety burden and slower iteration.
- Electron/.NET: runtime weight, higher idle memory, worse fit for NFR-3/NFR-7.

## Consequences
Strong ecosystem for QUIC and TLS. Contributors need Rust. OS APIs reached via
crates or `unsafe` FFI confined to `mono-input` backends.
