# Windows host to Mac client input check

Use the Windows x64 and Apple Silicon Mac packages from the same successful
GitHub Actions run. This checklist verifies physical input; passing CI alone is
not acceptance. Keep the old Mono Sync config unchanged. Do not run the old and
new input apps at the same time.

## Set up the pair

1. Install the Windows package and Mac DMG. Confirm both show Deskflow 1.27.0.
   On macOS, allow Accessibility and Input Monitoring only for Deskflow if
   macOS asks. Restart Deskflow only when macOS requests it.
2. On Windows, select **Use this computer's keyboard and mouse (server)**.
   Configure the Windows screen and Mac screen with the exact computer names
   shown in Deskflow. Place the Mac screen to match the physical layout.
3. On the Mac, select **Use another computer's mouse and keyboard (client)**.
   Enter the Windows host's current private LAN address. Allow Deskflow through
   Windows Firewall for the private network only. The default port is TCP
   `24800`; do not expose it to the public internet.
4. Keep TLS and fingerprint checking enabled. Before trusting a new peer,
   compare the presented SHA-256 fingerprint on both machines. Stop if they do
   not match or if either machine shows an unexpected fingerprint.
5. Start sharing only after both screen names, layout, and fingerprints check
   out. Do not enable automatic start for this test.

## Quick check (about 2 minutes)

1. Move the pointer across the configured edge to the Mac and back 20 times.
   Look for jumps, bounce-back, stalls, or loss of control.
2. In an empty text editor on the Mac, type:

   `Deskflow check: AaZz 0123 !? @#`

   Confirm the text matches exactly and arrives without perceptible delay. Try
   a harmless modifier shortcut in the editor (select the sample, then clear
   the selection), a function key with no destructive action, a click, and
   vertical scroll.
3. If a key is stuck, a fingerprint is unexpected, or control is lost, stop
   sharing and record the time and logs. Do not continue to file-transfer work.

## Acceptance check (15 minutes)

- Make at least 50 edge crossings. No teleport, bounce, stall, or lost control.
- Type this 200-character sample by hand, then compare it exactly:

  `AaZz 0123 !? @# QqWw 4567 $% MmNn 89() BbCc 12+=~!DeFg 3456 ^& RrTt 7890 [] HhJj 1357 () KkLl 2468~!PpOo 0246 <> YyUu 9753 {} AaEe 1122 ;; ZzXx 3344~!CcVv 5566 ** NnMm 7788 __ GgFf 9900 ++ DdSs 1234~!`

  Also test common modifier shortcuts and function keys. Verify no missing,
  duplicated, or stuck key/modifier, and confirm latency feels immediate.
- Exercise clicks, scrolling, return-to-host behavior, and the configured
  control hotkey.
- While on the Mac, disconnect the LAN with a key and mouse button held. Both
  must release on the Mac. Reconnect and repeat the quick checks without
  restarting either app.
- Record OS versions, screen names, monitor arrangement, resolution, scaling,
  result, and relevant logs. Do not include pairing codes or private keys.

**Go/no-go:** Only the user can accept input behavior on the actual pair. Do
not begin file-transfer work until every check passes and the user confirms
the result. If any check fails, keep file transfer deferred and report the
exact failure and logs.
