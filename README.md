# mono-sync

One keyboard and mouse across several computers on the same network, plus
quick file and clipboard transfer between them. Windows and macOS first;
open source; LAN-only; no accounts, no cloud.

> **Status: alpha.** Keyboard, mouse, text clipboard and file transfer work
> between two computers on one LAN. Tested end to end on loopback and Mac to
> Windows by hand. Not yet: 3+ computers, multi-monitor layouts, tray icon,
> installers, code signing. Expect rough edges.

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

## Use it

1. Download `mono-x86_64-pc-windows-msvc.exe` (Windows) or the macOS binary from
   the latest [release](https://github.com/mahammadosmanov/mono-sync/releases), or build:
   `cargo build --release -p mono-cli`.
2. Run it on **both** computers. A browser tab (control panel) opens.
3. Computer with the keyboard and mouse: choose **Host**, pick the side where
   the other computer sits, Save. It shows an address and a pairing code.
4. Other computer: choose **Guest**, enter the address and code, Save.
5. Push the cursor through the chosen edge. `Ctrl+Alt(Option)+Esc` returns control.
6. Copy text on one side and paste on the other. Drop files on the panel to send them.

Windows may show SmartScreen (unsigned): More info, Run anyway. macOS: grant
**Accessibility** and **Input Monitoring** to the app that launched `mono`
(Terminal), then restart it. Windows firewall: allow `mono` on private networks
if asked (UDP 24800 on the host). Settings live in `%APPDATA%\mono-sync` or
`~/Library/Application Support/mono-sync`.

Command line is still available: `mono host --edge left`, `mono guest <ip:port> <code>`,
`--no-browser` to skip opening the panel.

## Layout

| Path | Purpose |
|---|---|
| `crates/proto` | Wire messages |
| `crates/net` | QUIC transport, pairing handshake |
| `crates/input` | Layout router, host/guest engines, stuck-key guard, rdev OS glue |
| `crates/clip` | Text clipboard sync |
| `crates/cli` | The `mono` binary: runner, settings, control panel |
| `scripts/` | Loopback end-to-end test |
| `docs/` | PRD, architecture, ADRs, threat model, test plan |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security issues: [SECURITY.md](SECURITY.md).

## License

Licensed under either of [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT) at your option.
