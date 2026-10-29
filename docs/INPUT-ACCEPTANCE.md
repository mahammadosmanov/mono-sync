# Windows host to Mac client check

Use the Windows x64 setup and Apple Silicon macOS DMG built from the same source revision. Do not run an older keyboard/mouse sharing app at the same time. Build and unit-test results do not verify real-machine input or file transfer.

## Connect the pair

1. Install Mono Sync on Windows for the current user. Move Mono Sync from the macOS DMG into Applications. macOS may ask for input-control permission; Windows firewall policy may block a connection.
2. On Windows, select the server role. On the Mac, select the client role and enter the Windows computer's current private LAN address.
3. Match the screen names and physical screen layout shown in Mono Sync. Keep TLS and fingerprint checking enabled. Compare a new peer's SHA-256 fingerprint on both computers before trusting it.
4. Connect only after both names, layout, and fingerprints match. Use the default TCP port `24800`; keep it on the private network.

## Check keyboard and mouse

- Move across the configured edge and back, then test typing, modifiers, a harmless shortcut, clicks, scrolling, and returning to the host.
- If input is lost or a key stays held, disconnect sharing and record the time and relevant logs. Do not include pairing codes or private keys in a report.

The user has reported that Windows-to-Mac keyboard and mouse input works. This document does not claim an independent 15-minute hardware run.

## Check file transfer

1. With the trusted peer connected, send a small test file from Windows. On the Mac, confirm the sender name and file name, choose a destination, and wait for the verified completion message.
2. Compare the received file's contents or SHA-256 with the original. Confirm the original remains unchanged.
3. Repeat from Mac to Windows. If a file with the same name already exists, confirm Mono Sync leaves it untouched and reports the transfer failure.
4. Cancel one transfer and disconnect during another. Confirm partial files are removed and keyboard/mouse sharing remains usable or reconnects normally.

The receiver must explicitly choose where to save each file. Mono Sync transfers individual files up to 10 GiB, does not open received files, and does not overwrite existing files. Filesystems without exclusive same-directory hard-link support fail closed.

Record OS versions, screen names/layout, result, and relevant logs. Do not include private keys or pairing secrets.
