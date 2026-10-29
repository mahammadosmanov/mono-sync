# Windows portable package

Use the standalone `MonoSync-Windows-Portable.zip` handoff, or the
`mono-sync-windows-x64-portable` artifact from a run using the updated workflow.
Older workflow artifacts contain both an MSI and the portable ZIP; use only the
portable ZIP inside those older artifacts.

1. Extract the ZIP into a folder writable by your Windows account, such as a folder under `%LOCALAPPDATA%`.
2. Open the extracted `deskflow-1.27.0-monosync-win-x64-portable` folder and run `deskflow.exe`.
3. Keep the folder in place. Deskflow stores its settings, screen layout, and TLS data in its `settings` folder.

This package includes the runtime DLLs and runs as the current user. It does not install the Windows service or add firewall rules. It cannot control UAC prompts or the Windows login screen. If Windows Firewall or an organization policy blocks the private-network connection, an administrator may need to approve a firewall rule. Do not expose Deskflow's port to the public internet.

The Windows MSI is retained for administrator-managed deployments. It installs a service and firewall exceptions and can prompt for administrator approval, so it is not the standard-user package.
