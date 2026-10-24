# Roadmap

Each milestone ends with a tagged pre-release and an entry in `CHANGELOG.md`.
A milestone is closed only when its exit criteria are demonstrated.

| Milestone | Scope | Exit criteria |
|---|---|---|
| M0 Foundation | Repo, licenses, PRD, ADRs, CI, protocol, transport | CI green on Windows/macOS/Linux; transport tests pass |
| M1 Core control | Input capture/injection, edge switching, CLI `host`/`guest`, release-all safety | Mac host controls Windows guest and reverse, by hand-run checklist; no stuck keys |
| M2 Pairing and discovery | Persistent identity, pairing code flow, mDNS, auto-reconnect | Pair two machines without typing an IP |
| M3 Clipboard and files | Text clipboard, file send with checksum and resume | 1 GB file transfer verified by hash; throughput benchmark published |
| M4 App | Tray UI, layout editor, settings, autostart, key-mapping profiles | Non-technical user completes setup unaided |
| M5 Scale and hardening | 3+ machines, multi-monitor/DPI, TCP fallback decision, latency benchmark | NFR-1..NFR-4 measured and documented |
| M6 Release | Packaging (MSI/zip, dmg), signing decision, docs site, 1.0 | Installers built by CI from a tag |
