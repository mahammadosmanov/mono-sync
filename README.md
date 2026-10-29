# Mono Sync

Mono Sync is rebuilding its keyboard and mouse sharing app on the native Deskflow application and input engine. This branch contains Deskflow `v1.27.0` at upstream commit `081f6478e654a112168879e67a41ce7d7e4ae3af`.

This first phase builds the existing Deskflow GUI, server/client, TLS protocol, permissions flow, and operating system input backends. The former Mono Sync Rust/rdev input engine is not part of this branch. File transfer is deferred until input passes manual testing between the actual Windows host and Mac client.

## Build

Requirements and build options are in [Deskflow's developer guide](docs/dev/build.md). Minimums for this source: CMake 3.24, Qt 6.7, and OpenSSL 3. On macOS use `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build`, then `cmake --build build --target package`. On Windows, follow the documented MSVC, Qt, vcpkg, and WiX setup.

The GitHub Actions workflow builds upstream unit tests, then creates a macOS ARM64 DMG and Windows x64 portable ZIP when this branch is pushed. It uploads workflow artifacts only; it does not publish releases. The WiX MSI installs a Windows service and firewall rules, so it requires administrator approval and is not the normal-user download.

For Windows, use the [portable package instructions](docs/WINDOWS-PORTABLE.md). It runs from a writable folder under the current account and does not install the service. Windows firewall policy may still block incoming connections, and the portable build cannot control UAC prompts or the login screen. macOS still requires its own input-control permission.

## Acceptance

Build and unit-test results do not verify physical input. Follow the [Windows host to Mac client input check](docs/INPUT-ACCEPTANCE.md) and accept every item on the actual pair before file-transfer work begins.

## License

Deskflow is GPL-2.0-only with its documented OpenSSL linking exception. See [LICENSE](LICENSE), [LICENSES](LICENSES), and [upstream provenance](docs/UPSTREAM.md).
