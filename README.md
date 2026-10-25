# mono-sync

One keyboard and mouse across several computers on the same network, plus
quick file and clipboard transfer between them. Windows and macOS first;
open source; LAN-only; no accounts, no cloud.

> **Status: pre-alpha (M1).** Encrypted transport, routing logic and the
> `mono host` / `mono guest` commands are implemented and unit/loopback tested.
> **Not yet verified on real Mac + Windows hardware**: that is the next step.
> No installers yet; build from source.

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

## Try it (pre-alpha, build from source)

1. On both machines: `cargo build --release -p mono-cli` (binary: `target/release/mono`).
2. Allow UDP port 24800 in the host's firewall.
3. Host (the machine with the keyboard): `mono host --edge right`. It prints a pairing code.
4. Guest: `mono guest <host-ip>:24800 <pairing-code>`.
5. Push the cursor through the host's right edge. `Ctrl+Alt(Option)+Esc` returns control.

macOS: the terminal running `mono` needs Accessibility and Input Monitoring
(System Settings → Privacy & Security). The pairing code changes on every run.

## Layout

| Path | Purpose |
|---|---|
| `crates/proto` | Wire messages |
| `crates/net` | QUIC transport, pairing handshake |
| `crates/input` | Layout router, host/guest engines, stuck-key guard, rdev OS glue |
| `crates/clip` | Text clipboard sync |
| `crates/cli` | The `mono` binary |
| `docs/` | PRD, architecture, ADRs, threat model, test plan |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security issues: [SECURITY.md](SECURITY.md).

## License

Licensed under either of [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT) at your option.
