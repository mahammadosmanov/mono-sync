# Local patch to rdev 0.5.3

Upstream: https://github.com/Narsil/rdev (MIT). Changed files: `src/macos/common.rs`, `src/macos/simulate.rs`, `src/macos/grab.rs`, `src/macos/listen.rs`.
The key-press `Event::name` is no longer computed on macOS, because that calls
`TSMGetInputSourceProperty` off the main queue and macOS 14+ aborts the process
(EXC_BREAKPOINT in `dispatch_assert_queue`). mono-sync never reads `name`.
Remove this vendored copy when upstream fixes it.

## macOS drag and middle-button events (src/macos/common.rs, src/macos/simulate.rs)
While a button is held, macOS delivers `LeftMouseDragged`, `RightMouseDragged` and
`OtherMouseDragged` instead of `MouseMoved`. Upstream mapped only `MouseMoved`, so
during a drag no motion reached mono-sync and the Mac cursor moved on its own.
- Capture: all three Dragged types map to `EventType::MouseMove` at `cg_event.location()`.
- Capture: `OtherMouseDown`/`OtherMouseUp` map to `ButtonPress`/`ButtonRelease`. The button
  comes from `EventField::MOUSE_EVENT_BUTTON_NUMBER` (0 left, 1 right, 2 middle, others
  `Button::Unknown(n)`). The three OtherMouse types are added to the event mask.
- Injection: `Middle` and `Unknown(n)` post `OtherMouseDown`/`OtherMouseUp` with the button
  number set. Simulated presses are tracked in a static list (`HELD`). While one is held,
  `MouseMove` posts the matching Dragged type for the oldest held button, so windows drag and
  selections extend on the Mac. Only simulated presses are tracked; a physical button held on the
  Mac is not seen by this list.

## macOS event tap re-enable (src/macos/grab.rs, src/macos/listen.rs)
macOS disables a CGEventTap when a callback is slow, and when secure input or a system prompt
takes over. It then sends `kCGEventTapDisabledByTimeout` or `kCGEventTapDisabledByUserInput`
to the callback and delivers no events until the tap is enabled again. `raw_callback` now calls
`CGEventTapEnable(tap, true)` on those two types and returns the event unchanged. The tap
pointer is kept in a static `AtomicPtr` set right after `CGEventTapCreate`.

## Windows MouseMove injection (src/windows/simulate.rs)
`MouseMove` ignored the virtual desktop origin (`SM_XVIRTUALSCREEN`/`SM_YVIRTUALSCREEN`), so with
several monitors the injected cursor landed on the wrong monitor. The origin is now subtracted and
coordinates are normalised over `size - 1`.

Update: `MouseMove` now uses `SetCursorPos` (screen pixels) instead of SendInput absolute
coordinates, which were unreliable with several monitors / mixed DPI.
