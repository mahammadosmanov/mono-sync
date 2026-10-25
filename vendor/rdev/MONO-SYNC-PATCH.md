# Local patch to rdev 0.5.3

Upstream: https://github.com/Narsil/rdev (MIT). Changed file: `src/macos/common.rs`.
The key-press `Event::name` is no longer computed on macOS, because that calls
`TSMGetInputSourceProperty` off the main queue and macOS 14+ aborts the process
(EXC_BREAKPOINT in `dispatch_assert_queue`). mono-sync never reads `name`.
Remove this vendored copy when upstream fixes it.
