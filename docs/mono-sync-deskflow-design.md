# Mono Sync: Deskflow-based input foundation

**Status:** Draft for user review. Deskflow source and the input-first staging are selected; implementation awaits review of this design and a later plan.

## Goal

Replace the custom mono-sync input engine with the native Deskflow application as the foundation for a reliable Windows-host to Mac-client keyboard and mouse workflow. Add file transfer only after the input workflow passes the real-machine acceptance gate below.

## User intent and constraints

- Use Deskflow's existing open-source source and native app, rather than reimplementing its input engine or wrapping the existing mono-sync Rust engine.
- Validate Windows x64 as the keyboard/mouse host and Apple Silicon macOS as the client.
- Keep the existing mono-sync repository, installed app, and user configuration intact until the new input workflow is accepted.
- Defer file transfer to a separate phase after input acceptance.
- Do not start capture or an input-sharing session automatically.

## Evidence and source pin

- Pin the foundation to Deskflow stable release `v1.27.0`, source commit `081f6478e654a112168879e67a41ce7d7e4ae3af` (released 2026-10-01). The official release list distinguishes this stable release from the later `v1.27.0.10` continuous prerelease: <https://github.com/deskflow/deskflow/releases>.
- Deskflow's repository identifies GPL-2.0, supports Windows 10 v1809+ and macOS 12+ for local builds; its Apple Silicon CI builds require macOS 13+: <https://github.com/deskflow/deskflow>, <https://github.com/deskflow/deskflow/blob/master/LICENSE>.
- Deskflow documents TLS as enabled by default and its existing GUI as the supported user flow: <https://github.com/deskflow/deskflow>.
- Upstream's current build guide lists CMake 3.24+, Qt 6.7+, OpenSSL 3.0+, Visual Studio 2022/MSVC + Windows SDK, Ninja, vcpkg, and WiX on Windows; on macOS it lists Xcode, Homebrew, Qt 6.7+, and OpenSSL: <https://github.com/deskflow/deskflow/wiki/Building>. The guide labels itself work in progress, so verify the pinned tag's CMake presets and workflow before executing the plan.
- This Mac has CMake 4.4.3, Apple Clang 21, Command Line Tools, OpenSSL 4.0.3, and pkgconf. Full Xcode, Qt 6, Ninja, and `xcodebuild` are absent. Upstream's guide lists Xcode, but whether Command Line Tools suffice for the pinned tag is not confirmed; this is not yet a confirmed blocker. No dependencies have been installed and no build has been attempted.
- The Deskflow tag SHA was verified through GitHub's commit API. A local clone attempt stalled in Git transport and was interrupted before checkout; no Deskflow source files are present yet. After this design is reviewed, clone the pinned tag with submodules into `source/` under this workspace.

## Phase 1 design

1. Build Deskflow `v1.27.0` as a native application from its pinned source. Preserve its GUI, server/client lifecycle, protocol, TLS, permissions handling, and OS-specific input capture/injection. Do not port or retain the custom `crates/input` host/guest engine in this product.
2. Keep the first baseline unmodified apart from build/packaging metadata needed to build and run it. Use Deskflow's UI and configuration for the hardware test; avoid a new wrapper or settings interface.
3. Produce separate Windows x64 and macOS ARM64 builds from the same source pin. Keep them side-by-side with the current mono-sync installation and configuration. Do not rebrand or migrate settings before the baseline is accepted; later packaging must use its own app identifier and profile directory.
4. Preserve upstream copyright, GPL-2.0 text, and third-party notices. Before redistributing a derivative, include the corresponding source and notices required by the license.
5. File transfer is out of scope for this phase. Deskflow's existing clipboard behavior may remain enabled; clipboard sharing is not file transfer.

## Phase 1 acceptance gate

CI/build success alone does not accept physical input behavior. Test on the user's actual Windows host and Mac client, on their normal LAN and display setup. Record OS versions, monitor layout, resolution, scaling, and Deskflow logs. The user must sign off that input feels responsive and stable before file transfer work begins.

- Complete a 15-minute session with at least 50 cursor edge crossings, with no cursor teleport, bounce, stall, or lost control.
- Type a known 200-character sample containing uppercase/lowercase letters, digits, punctuation, plus common modifier shortcuts and function keys. Output must match exactly, with no stuck key or modifier; the user confirms there is no perceptible input delay.
- Exercise clicks and scrolling on the Mac client, the configured take-control hotkey, and return-to-host behavior.
- Disconnect the LAN while holding a key and a mouse button; verify both are released on the client. Reconnect and repeat the input checks without restarting either app.
- Verify the actual DPI/resolution configuration and each display involved in the reported workflow. CI and unit tests cannot substitute for this manual check.

If any item fails, fix and retest the input phase. Do not begin file transfer or replace the existing mono-sync installation based on a green build alone.

## Alternatives considered

- **Use the exact archived Input Leap repository.** It is the closest literal match to the request and has Windows/macOS support, but its GitHub repository is archived/read-only and its latest stable release is v3.0.3 from 2025-06-13. This would put upstream maintenance on this project: <https://github.com/input-leap/input-leap>, <https://github.com/input-leap/input-leap/releases>.
- **Use Deskflow (selected).** Deskflow documents itself as the active upstream of Input Leap and publishes newer stable releases; it preserves the same native keyboard/mouse-sharing product lineage while reducing the burden of maintaining an archived base: <https://github.com/deskflow/deskflow/wiki/Project-Forks>, <https://github.com/deskflow/deskflow/releases>.
- **Keep mono-sync's custom Rust input engine and patch it further.** This preserves current code but does not meet the user's selected goal of building from Input Leap/Deskflow source, and it has not produced acceptable behavior. Retain the existing app only as a fallback until the new baseline passes.

## Risks and limits

- This is a GPL-2.0 project; a distributed derivative must preserve required notices and source access.
- The local Mac lacks Qt 6 and Ninja, and full Xcode is absent. The implementation plan will verify requirements against the pinned source and reuse installed tools where possible; missing build dependencies have not been installed. If a full Xcode installation is actually required, report its download and disk impact before proceeding.
- Physical input permissions, DPI behavior, keyboard latency, and cross-machine reconnect behavior remain unverified until tested on the actual Windows/Mac pair.
- File transfer will need its own design, security model, transfer integrity tests, and acceptance gate after Phase 1.
