# Deskflow source provenance

Mono Sync's Phase 1 input foundation is Deskflow `v1.27.0`, commit `081f6478e654a112168879e67a41ce7d7e4ae3af`.

- Upstream repository: https://github.com/deskflow/deskflow
- Pinned source archive: https://codeload.github.com/deskflow/deskflow/tar.gz/refs/tags/v1.27.0
- Retrieved archive SHA-256: `dfe4327f5d0631ead9bacee743a96ee58024af11b5cc080af89b4c740b18e893`
- Upstream license: GPL-2.0-only with an OpenSSL linking exception. Preserve `LICENSE`, `LICENSES/`, and all per-file notices when modifying or redistributing the application.
- A small local CMake option, `DESKFLOW_USE_GIT_VERSION`, defaults to `OFF`. This preserves the pinned source version `1.27.0.0` in this repository, whose inherited Git history contains unrelated Mono Sync tags. Developers can opt into upstream's Git-derived version behavior explicitly.

Deskflow's GUI, input engine, TLS protocol, and platform backends remain upstream-owned. The branch adds a small package-version check to CI and a manual acceptance checklist. No file-transfer implementation is included.
