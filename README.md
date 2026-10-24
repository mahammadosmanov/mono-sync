# mono-sync

One keyboard and mouse across several computers on the same network, plus
quick file and clipboard transfer between them. Windows and macOS first;
open source; LAN-only; no accounts, no cloud.

> **Status: pre-alpha.** The protocol and encrypted transport exist and are
> tested. Input capture/injection and the CLI are in progress (milestone M1).
> There is nothing to install yet.

## Goals

- Cross-OS compatibility first: physical-key mapping, Cmd/Ctrl profiles, DPI-aware edges.
- Input that feels local (target < 5 ms added latency on a wired LAN).
- Safe by default: TLS 1.3 (QUIC), pinned host identity, pairing token.
- Single binary per OS, installable without admin rights.

Full details: [PRD](docs/prd.md) · [Architecture](docs/architecture.md) ·
[Roadmap](docs/roadmap.md) · [ADRs](docs/adr/README.md) ·
[Threat model](docs/threat-model.md) · [Testing](docs/testing.md)

## Build

```sh
cargo build --workspace
cargo test --workspace
```

Linux needs `libx11-dev libxi-dev libxtst-dev`. macOS needs Accessibility and
Input Monitoring permission once input features land.

## Layout

| Path | Purpose |
|---|---|
| `crates/proto` | Wire messages |
| `crates/net` | QUIC transport, pairing handshake |
| `docs/` | PRD, architecture, ADRs, threat model, test plan |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security issues: [SECURITY.md](SECURITY.md).

## License

Licensed under either of [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT) at your option.
