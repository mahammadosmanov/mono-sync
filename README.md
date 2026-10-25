# mono-sync

One keyboard and mouse across several computers on the same network, plus
quick file and clipboard transfer between them. Windows and macOS first;
open source; LAN-only; no accounts, no cloud.

> **Status: alpha.** Keyboard, mouse, text clipboard and file transfer work
> between two computers on one LAN. Tested end to end on loopback and Mac to
> Windows by hand. Not yet: 3+ computers, multi-monitor layouts, auto-start,
> code signing. Expect rough edges.

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

1. Install from the latest [release](https://github.com/mahammadosmanov/mono-sync/releases):
   Windows `mono-sync_*_x64-setup.exe` (installs for your user, no admin) or the
   portable `mono-sync-portable-windows-x64.exe`; macOS `.dmg`. Needs the WebView2
   runtime on Windows (already present on Windows 11 and current Windows 10).
2. Open **mono-sync** on **both** computers.
3. Computer with the keyboard and mouse: choose **This computer has the keyboard and mouse**,
   pick the side where the other computer sits, **Save and start**. It shows an
   address and a pairing code.
4. Other computer: choose **Control this computer from another**, enter the
   address and code, **Save and start**.
5. Push the cursor through the chosen edge. `Ctrl+Alt(Option)+Esc` returns control.
6. Copied text is shared automatically. Drop files on the window to send them.
   Closing the window keeps sharing running in the tray icon; Quit is in the tray menu.

Unsigned builds: Windows SmartScreen says More info, Run anyway; macOS says
right-click, Open (and allow in Privacy & Security). macOS needs **Accessibility**
and **Input Monitoring** for mono-sync. Windows firewall: allow mono-sync on
private networks if asked. Settings: `%APPDATA%\mono-sync` or
`~/Library/Application Support/mono-sync`.

Headless: `mono host --edge left`, `mono guest <ip:port> <code>` (crate `mono-cli`).

## Layout

| Path | Purpose |
|---|---|
| `crates/proto` | Wire messages |
| `crates/net` | QUIC transport, pairing handshake |
| `crates/input` | Layout router, host/guest engines, stuck-key guard, rdev OS glue |
| `crates/clip` | Text clipboard sync |
| `crates/core` | Settings, identity, host/guest loops (no UI) |
| `crates/app` | Tauri desktop app (window, tray) |
| `crates/cli` | Headless `mono` command |
| `docs/` | PRD, architecture, ADRs, threat model, test plan |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security issues: [SECURITY.md](SECURITY.md).

## License

Licensed under either of [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT) at your option.
