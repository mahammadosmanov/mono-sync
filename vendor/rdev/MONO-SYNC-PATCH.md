# Local patch to rdev 0.5.3

Upstream: https://github.com/Narsil/rdev (MIT). Changed file: `src/macos/common.rs`.
The key-press `Event::name` is no longer computed on macOS, because that calls
`TSMGetInputSourceProperty` off the main queue and macOS 14+ aborts the process
(EXC_BREAKPOINT in `dispatch_assert_queue`). mono-sync never reads `name`.
Remove this vendored copy when upstream fixes it.

## Windows MouseMove injection (src/windows/simulate.rs)
`MouseMove` ignored the virtual desktop origin (`SM_XVIRTUALSCREEN`/`SM_YVIRTUALSCREEN`), so with
several monitors the injected cursor landed on the wrong monitor. The origin is now subtracted and
coordinates are normalised over `size - 1`.

Update: `MouseMove` now uses `SetCursorPos` (screen pixels) instead of SendInput absolute
coordinates, which were unreliable with several monitors / mixed DPI.
