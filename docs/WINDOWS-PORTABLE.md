# Windows per-user setup

Run `MonoSync-Setup.exe` as the current Windows user. It installs under `%LOCALAPPDATA%\Programs\Mono Sync`, creates current-user Start menu shortcuts, and does not require administrator access.

Close Mono Sync before updating it. Setup checks every packaged file and the known files it replaces before changing the installation. If Windows reports a file is locked, close Mono Sync completely and choose Retry. Cancel setup if the file remains locked; setup will not continue past a failed write or offer an Ignore option that could leave mixed versions.

The app stores its Qt settings and TLS files in `%APPDATA%\Mono Sync`. On first launch, it copies the known settings file, TLS certificate, and peer-trust databases from `%APPDATA%\Deskflow` only when the new Mono Sync profile does not exist. The old profile is left untouched. It does not import unrelated files or follow profile symlinks.

To update, close Mono Sync and run the newer setup. The installer replaces packaged files and preserves the user profile. If Mono Sync is still running, close it and retry; setup does not force-kill it. Automatic update checks are disabled until Mono Sync has an update feed.

To remove Mono Sync, use the Start menu's **Uninstall Mono Sync** shortcut or the current-user Installed Apps entry. The uninstaller removes its packaged files and shortcuts, but preserves `%APPDATA%\Mono Sync`. Remove that folder separately only if you also want to delete its settings and TLS trust.

Windows or organization firewall policy may block private-network connections. Mono Sync does not change that policy; an administrator may need to approve a firewall rule. Do not expose the sharing port to the public internet. Standard-user mode cannot control UAC secure desktop or the Windows login screen.
