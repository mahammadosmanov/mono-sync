# Mono Sync

Mono Sync is a Windows and macOS app for sharing one keyboard and mouse across computers, with explicit one-file transfers over the existing authenticated peer connection. Its input engine and native platform backends come from Deskflow `v1.27.0` at commit `081f6478e654a112168879e67a41ce7d7e4ae3af`; Mono Sync-specific changes include product identity, safe profile migration, standard-user Windows packaging, and file transfer.

The Windows x64 host and Apple Silicon Mac client use the same TLS-protected Deskflow peer connection. A receiving user chooses the destination; Mono Sync verifies size and SHA-256, writes to a private temporary file, and refuses to overwrite a file that appears at the destination. Folders, resume, and automatic file opening are not supported.

## Build

Build requirements and platform options are in [the upstream-derived developer guide](docs/dev/build.md). The product version is `0.2.1`; the source foundation remains pinned to Deskflow `v1.27.0`. GitHub Actions builds and tests Windows x64 and macOS ARM64 packages and uploads artifacts only. It does not publish releases.

For Windows, use the [per-user setup instructions](docs/WINDOWS-PORTABLE.md). The setup, app, and uninstaller run as the current user. They do not install a service or change firewall rules. Windows or organization firewall policy may still block connections. macOS may request its input-control permission; Mono Sync cannot bypass operating-system permission prompts.

Update checks are disabled until Mono Sync has its own update feed. Install a newer Windows setup or macOS DMG manually from a verified project build.

## Verification

Builds and unit tests do not prove input or file transfer on actual machines. Follow the [Windows host to Mac client checklist](docs/INPUT-ACCEPTANCE.md) on the intended pair. The user has confirmed the keyboard and mouse workflow works; file-transfer acceptance still needs a real Windows-to-Mac send and receive.

## License

Mono Sync is distributed under GPL-2.0-only with the documented OpenSSL linking exception. See [LICENSE](LICENSE), [LICENSES](LICENSES), and [Deskflow source provenance](docs/UPSTREAM.md). Deskflow and earlier Synergy contributors remain credited in the app and source.
