# Deskflow source provenance

Mono Sync `0.2.3` is built from Deskflow `v1.27.0`, source commit `081f6478e654a112168879e67a41ce7d7e4ae3af`.

- Upstream repository: https://github.com/deskflow/deskflow
- Pinned source archive: https://codeload.github.com/deskflow/deskflow/tar.gz/refs/tags/v1.27.0
- Retrieved archive SHA-256: `dfe4327f5d0631ead9bacee743a96ee58024af11b5cc080af89b4c740b18e893`
- Upstream license: GPL-2.0-only with an OpenSSL linking exception. Preserve `LICENSE`, `LICENSES/`, and all per-file notices when modifying or redistributing the application.

Mono Sync keeps Deskflow's native input engine, OS backends, and authenticated TLS peer connection. Its product name, bundle and settings identifiers, package names, assets, and one-file transfer UI are Mono Sync-specific. The file-transfer extension stages files privately, requires receiver consent, validates size and SHA-256, and refuses destination overwrites.

The CMake option `DESKFLOW_USE_GIT_VERSION` remains disabled in release builds so Git history cannot replace Mono Sync's product version with an unrelated tag. Deskflow's upstream version is documented here; the user-facing product version is `0.2.3`.
