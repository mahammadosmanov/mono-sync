# 0014. Draw our own cursor on Windows PCs that have no mouse

- Status: Accepted
- Date: 2026-10-25

## Context
On a Windows PC with no mouse device attached, Windows draws no pointer. Injected
input moves the cursor (the OS reports the right coordinates, verified from the
guest log) but nothing is visible, and `SetCursorPos` or a nudge via `SendInput`
does not change that. A guest PC without a mouse is the normal case for this app.

## Decision
When `GetSystemMetrics(SM_MOUSEPRESENT)` is 0, or the user ticks "draw it myself",
the guest shows a small click-through, always-on-top layered window containing
an arrow and moves it with every cursor update. It is hidden when control
returns to the host or the link drops.

## Alternatives considered
- Virtual HID mouse driver: needs a signed driver and admin rights (NFR-6).
- Mouse Keys accessibility mode: changes how the number pad behaves.
- Telling users to plug in a mouse: defeats the purpose.

## Consequences
The arrow is a fixed 12x19 px image, not the user's pointer scheme, and does not
scale with DPI yet. It cannot appear above the secure desktop (UAC, lock screen).
Not verified on hardware at the time of writing.
