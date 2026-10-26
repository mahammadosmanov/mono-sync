# 0015. Use MouseKeys to make Windows draw the pointer on a PC without a mouse

- Status: Accepted
- Date: 2026-10-25
- Supersedes: 0014

## Context
On a Windows PC with no mouse device attached, Windows draws no pointer. Injected
input moves the cursor (the OS reports the right coordinates), but nothing is
visible. A guest PC without a mouse is the normal case for this app.

ADR-0014 drew a pointer ourselves in a layered window. The owner rejected it: the
pointer is not Windows' own, it is a fixed image, and it is a second workaround
on top of the OS. The owner asked for Input Leap's solution.

## Decision
While the cursor is on the guest and `GetSystemMetrics(SM_MOUSEPRESENT)` is 0, the
guest turns on Windows MouseKeys with `SystemParametersInfoW(SPI_SETMOUSEKEYS, ...,
SPIF_SENDCHANGE)`. The flags are `MKF_AVAILABLE | MKF_MOUSEKEYSON`, plus
`MKF_REPLACENUMBERS` when NumLock is on. `MKF_HOTKEYACTIVE` is left off. With
MouseKeys on, Windows draws its own pointer.

This is the technique of Input Leap (`MSWindowsScreen::forceShowCursor` and
`updateForceShowCursor`). The Win32 calls and flags come from the MOUSEKEYS
documentation.

- The user's MouseKeys and hide-while-typing settings are read and saved first, and
  written back when the cursor leaves the guest, when the link drops, when the app
  stops, or when the app exits.
- While the cursor is on the guest, a check runs every 500 ms (`os::CURSOR_CHECK`).
  It re-enables MouseKeys and turns hide-while-typing off again if another app or
  Windows switched them back. It reads first and writes only if a value differs. It
  restores the settings if a mouse was plugged in.
- Hide-while-typing (MouseVanish, `SPI_SETMOUSEVANISH`) is turned off while the
  cursor is on the guest. Keys forwarded from the other computer would otherwise
  hide the pointer until a real mouse move, and `SetCursorPos` does not show it again.
- If `GetCursorInfo` still reports the pointer hidden after the check, a 1 px
  relative move and back (`SendInput`, `MOUSEEVENTF_MOVE`) wakes it, at most once a
  second.
- Visibility is checked with `GetCursorInfo` (`CURSOR_SHOWING`) and written to the
  log.
- Nothing changes while a mouse is attached.

The state lives in `Core`, not in the guest task, so that stopping a loop restores
MouseKeys before the next role starts. A generation counter stops a loop that has
already been stopped from changing MouseKeys again.

## Alternatives considered
- Self-drawn overlay window (ADR-0014): rejected by the owner. It is not Windows'
  pointer and does not follow the user's pointer scheme or DPI.
- Virtual HID mouse driver: needs a signed driver and admin rights (NFR-6).
- Telling users to plug in a mouse: defeats the purpose.

## Consequences
- While the cursor is on the guest PC, MouseKeys is on. The numeric keypad moves
  the pointer instead of typing digits. This is restored when the cursor leaves.
- The setting is system-wide, and the change is broadcast to other windows, so
  Settings and other apps may see it flip. Hide-while-typing is switched the same
  way.
- Hide-while-typing (MouseVanish) is saved with MouseKeys and written back on the
  same occasions. If the user turns it on while the cursor is on the guest, it is
  turned off again within 500 ms, and the value from before is restored on leave.
- If the app crashes or is killed, no restore runs and MouseKeys (and, if it was
  changed, hide-while-typing) can stay as set until the user switches it back in
  Ease of Access. On startup nothing is restored, since the app cannot tell its own
  leftover change from a user setting. The user has to switch them back by hand in
  that case.
- Input on the secure desktop (UAC prompts, lock screen, Ctrl+Alt+Del) cannot be
  injected from this process. Three failed injections in a row make the guest log
  `Windows is showing a protected screen (UAC/lock): input paused` once; the next
  success logs `Input resumed` once. The code cannot tell the secure desktop from
  another injection failure, so the note names the likely cause only.
- Not tested on Windows hardware. The Windows code is compile-checked with
  `clippy --target x86_64-pc-windows-msvc` only.
