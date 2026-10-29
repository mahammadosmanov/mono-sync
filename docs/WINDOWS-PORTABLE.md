# Windows portable package

Use the standalone `MonoSync-Windows-Portable.zip` handoff, or the
`mono-sync-windows-x64-portable` artifact from a run using the updated workflow.
Older workflow artifacts contain both an MSI and the portable ZIP; use only the
portable ZIP inside those older artifacts.

1. Extract the ZIP into a folder writable by your Windows account, such as a folder under `%LOCALAPPDATA%`.
2. Open the extracted `deskflow-1.27.0-monosync-win-x64-portable` folder and run `deskflow.exe`.
3. Keep the folder in place. Deskflow stores its settings, screen layout, and TLS data in its `settings` folder.

Windows builds always run the core in the current user's desktop session. The GUI does not start the background service, request elevation, install startup registry entries, or change firewall rules. Settings and TLS data use the extracted `settings` folder when it exists, or the current user's roaming profile if it does not. Removing the portable marker does not enable service mode or switch to machine-wide settings.

Updates are manual. Extract the newer portable ZIP into a new user-writable folder. Close Deskflow, copy the existing `settings` folder into the new folder if you want to keep the server layout and TLS trust, then run `deskflow.exe` from the new folder. Delete the old folder after confirming the new copy works. The updater setting is disabled in Windows builds so the application does not direct users to an administrator-required installer.

To remove the portable copy, close Deskflow and delete its extracted folder. If you removed the portable settings marker earlier, the user's settings remain under `%APPDATA%\Deskflow`; delete that folder only if you also want to remove those settings and TLS data.

Windows or organization firewall policy may still block private-network connections. Deskflow does not change that policy; an administrator may need to approve a firewall rule. Do not expose Deskflow's port to the public internet. Standard-user mode also cannot control UAC secure desktop or the Windows login screen.

The Windows MSI is retained for administrator-managed deployments. It installs a service and firewall exceptions and can prompt for administrator approval, so it is not the standard-user package.
