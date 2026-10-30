# Mono Sync product direction

**Status:** Implementation on `codex/deskflow-rebuild`. The user selected Deskflow as the native input foundation and confirmed that Windows-host to Mac-client keyboard and mouse sharing works. File transfer is implemented and awaits real two-machine acceptance.

## Product

Mono Sync is a Windows x64 and Apple Silicon macOS app built from Deskflow `v1.27.0`, source commit `081f6478e654a112168879e67a41ce7d7e4ae3af`. It keeps Deskflow's native input engine, TLS peer connection, and platform backends. Product-facing names, application identifiers, assets, packages, and profile namespace are Mono Sync. Do not replace the native engine with the retired Rust input path.

The app offers one-file transfer over the already authenticated and connected peer. The sender selects a file; the receiver selects a destination and accepts it. Transfers are size-limited, chunked, SHA-256 checked, written through a private temporary file, and finalized without overwriting an existing destination. Folders, resume, and automatic opening are not supported. Existing v1.8 peers remain input-compatible; file transfer requires the Mono Sync protocol capability and a verified peer identity.

## Identity and data

- Product version: `0.2.1`; upstream source version: `1.27.0.0`.
- Product application ID: `dev.monosync.app`.
- Preserve required Deskflow, Synergy, GPL-2.0-only, OpenSSL-exception, and third-party notices. See [UPSTREAM.md](UPSTREAM.md), `LICENSE`, and `LICENSES/`.
- On first launch only, import allowlisted Deskflow settings, certificate, and trust files if the Mono Sync profile does not exist. Preserve source files, exact certificate/trust bytes, and any existing Mono Sync profile. Never interpret the former Rust app's JSON settings as Qt settings.
- Windows setup, app, and uninstaller run as the current user. No service, machine-wide firewall rule, or elevation is part of the normal workflow. Operating-system firewall policy and macOS input-control authorization remain controlled by the OS.
- Automatic update checks are disabled until Mono Sync has an update source.
- Do not start an input-sharing session or trust a new peer automatically.

## Verification boundary

CI and local tests cover builds, protocol compatibility, consent, file integrity, no-overwrite behavior, bounded IPC, and standard-user package paths. They cannot prove Windows-to-Mac file transfer on the user's hardware. Keep the user's confirmed keyboard/mouse result separate from file-transfer acceptance. Before calling transfer accepted, send one representative file in both directions, verify its bytes and destination behavior, then test cancel/disconnect and reconnect without losing input.
