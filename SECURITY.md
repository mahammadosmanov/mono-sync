# Security Policy

mono-sync forwards keyboard and mouse input between computers, so security
bugs can mean remote control or keystroke exposure. Please report them privately.

## Reporting

Use GitHub's **private vulnerability reporting** on this repository
(Security → Report a vulnerability). Do not open a public issue.
Include version, OS, reproduction steps and impact.

Expected response: acknowledgement within 7 days; a fix or mitigation plan
within 30 days for confirmed high-severity issues.

## Supported versions

Pre-1.0: only the latest release and `main`.

## Scope and design

See `docs/threat-model.md` and ADR-0004 for the trust model. In scope: pairing,
authentication, encryption, input injection abuse, file-transfer path handling.
Out of scope: an attacker who already controls an endpoint machine or can run
code as the user.
