# 0016. Windows Firewall rule added by the app with one UAC approval

- Status: Accepted
- Date: 2026-10-26

## Context
The host listens on UDP 24800 (ADR-0003). Windows Defender Firewall blocks
inbound UDP for a program unless a rule allows it. Windows asks once, the first
time a program listens, and stores the answer as a rule for that program's path.
Later runs do not prompt. If the rule is missing, for example because the app
runs from another folder after an update or move, inbound packets are dropped
without any message, and the guest times out.

Adding a rule needs administrator rights (UAC). The installer is per user
(ADR-0013) and does not run elevated, so it cannot add the rule at install time.

## Decision
- While it runs as a Windows host, the app checks for its rule with
  `netsh advfirewall firewall show rule name=mono-sync verbose`, which needs no
  admin rights. The answer is cached for 10 s, since the UI polls every second.
  If Windows prints the program path, the check also requires it to match this
  exe; otherwise only the rule name is checked.
- When the rule is missing, the app shows a banner with one button. The button
  runs `netsh advfirewall firewall add rule` elevated through PowerShell
  `Start-Process -Verb RunAs`, so Windows shows one UAC prompt. The app checks
  again afterwards and reports an error if the rule is still missing.
- The rule is inbound, UDP, local port 24800, `profile=private`, and scoped to
  this program's path.
- On other systems the check always passes and nothing is changed.

## Alternatives considered
- Per-machine installer that adds the rule at install time: rejected for now.
  It turns the per-user installer into an admin install, a bigger change than
  this problem needs. Revisit if users keep missing the rule.
- Turn the firewall off or allow all programs: rejected. It weakens the PC for
  every program, not just mono-sync.
- Ask users to allow it by hand: rejected as the main fix. The one-time prompt is
  easy to miss or dismiss, and the failure is silent.
- Also allow Public networks: rejected. The app is LAN-only (ADR-0007), and
  Public is the profile Windows gives networks it does not trust.

## Consequences
- A moved or renamed exe has no matching rule. The banner returns, and the same
  button adds a rule for the new path. Rules for old paths stay in Windows
  Defender Firewall under the name `mono-sync`; the user may delete them there.
- The rule applies only on Private networks. If Windows classes the LAN as
  Public, the button does not help and the network must be set to Private. The
  check does not read the profile, so this case shows no banner.
- While the app runs as a Windows host, `netsh` runs about every 10 s.
- The PowerShell script is passed with `-EncodedCommand`, so the exe path never
  goes through command-line quoting. Inside the script, single quotes in the path
  are doubled (`ps_quote`).
- The netsh and PowerShell calls live in `mono-core::firewall`. This is an
  exception to CONTRIBUTING's rule "No OS calls outside `mono-input` backends".
  It stays in core because the check is not about input. Move it to `mono-input`
  if the rule should stay strict.
- The path check is skipped when the path is not ASCII (netsh prints in the
  console code page, which can garble it) or when Windows prints no `Program:`
  line (for example a localised Windows). Then only the rule name is checked.
- Not tested on Windows hardware. The Windows code was compile-checked with
  `clippy --target x86_64-pc-windows-msvc`. The pure parts (output parsing, quoting,
  base64) are unit tested on macOS. The manual checklist (`docs/testing.md`)
  covers the UAC run.
