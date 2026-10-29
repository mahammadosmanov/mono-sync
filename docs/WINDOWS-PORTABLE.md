# Windows per-user setup

Use the `MonoSync-Setup.exe` handoff or the `mono-sync-windows-x64-setup`
artifact from a successful workflow run. The setup requests standard-user
execution and installs only for the current Windows account.

1. Run `MonoSync-Setup.exe` and follow the setup screens. It installs under `%LOCALAPPDATA%\Programs\Mono Sync` and creates current-user Start menu shortcuts.
2. Start Mono Sync from the Start menu. Its settings, screen layout, and TLS data are stored in `%APPDATA%\Deskflow`.

The Windows app always runs the core in the current user's desktop session. The setup and GUI do not request elevation, install a background service, add startup entries, or change firewall rules. Both the setup and app executables use standard-user execution manifests. Installation and uninstallation touch only the current user's program folder, Start menu, and uninstall registry entry.

To update, close Mono Sync and run the newer `MonoSync-Setup.exe`. It replaces packaged program files in the same per-user folder and preserves `%APPDATA%\Deskflow`, including screen layout and TLS trust. If Deskflow is still running, close it and retry; setup does not force-kill it.

If migrating from the earlier portable ZIP, close Deskflow and copy the contents
of its `settings` folder (including `Deskflow.conf` and `tls`, if present) into
`%APPDATA%\Deskflow` before the first setup launch. The old extracted folder is
left untouched and can be removed after confirming the migrated settings work.

To remove Mono Sync, use the Start menu's **Uninstall Mono Sync** shortcut or the current-user Installed Apps entry. The uninstaller removes packaged files and shortcuts, but preserves `%APPDATA%\Deskflow`. Delete that folder separately only if you also want to remove settings and TLS trust.

Windows or organization firewall policy may still block private-network connections. Deskflow does not change that policy; an administrator may need to approve a firewall rule. Do not expose Deskflow's port to the public internet. Standard-user mode also cannot control UAC secure desktop or the Windows login screen.

The workflow uploads the per-user setup as the default Windows artifact. A portable ZIP remains available inside the build job as its input payload. The separate WiX MSI installs a service and firewall exceptions and can request administrator approval; it is not the normal Windows package.
