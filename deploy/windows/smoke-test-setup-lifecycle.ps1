param(
  [Parameter(Mandatory = $true)]
  [string] $SetupPath
)

$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$adminSid = [Security.Principal.WindowsBuiltInRole]::Administrator
Write-Output "CI smoke token is in Administrators group: $($principal.IsInRole($adminSid))"
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\Mono Sync'
$profileDir = Join-Path $env:APPDATA 'Mono Sync'
$profileFile = Join-Path $profileDir 'Mono Sync.conf'
$profileSentinel = Join-Path $profileDir "MonoSync-Setup-Smoke-$([guid]::NewGuid()).txt"
$customFile = Join-Path $installDir 'user-owned-smoke-test.txt'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Mono Sync'
$userShortcut = Join-Path $startMenu 'user-owned-smoke-test.lnk'
$deskflowStartMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Deskflow'
$uninstallKey = 'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\Mono Sync'
$legacyInstallFiles = @(
  'deskflow.exe',
  'deskflow-core.exe',
  'deskflow-daemon.exe',
  'deskflow-client.exe',
  'deskflow-server.exe',
  'translations\deskflow_en.qm',
  'translations\deskflow_es.qm',
  'translations\deskflow_it.qm',
  'translations\deskflow_ja.qm',
  'translations\deskflow_ko.qm',
  'translations\deskflow_ru.qm',
  'translations\deskflow_zh_CN.qm'
) | ForEach-Object { Join-Path $installDir $_ }
$legacyShortcuts = @(
  (Join-Path $startMenu 'Deskflow.lnk'),
  (Join-Path $startMenu 'Uninstall Deskflow.lnk'),
  (Join-Path $deskflowStartMenu 'Deskflow.lnk'),
  (Join-Path $deskflowStartMenu 'Uninstall Deskflow.lnk')
)
$sentinel = 'setup-smoke-test-user-profile-preserved'
$createdProfile = -not (Test-Path -LiteralPath $profileDir)
$createdProfileFile = -not (Test-Path -LiteralPath $profileFile)
$existingProfileHash = $null

if (Test-Path -LiteralPath $installDir) {
  throw "Refusing to overwrite a pre-existing test install: $installDir"
}
if (Test-Path -LiteralPath $uninstallKey) {
  throw "Refusing to overwrite a pre-existing uninstall registration: $uninstallKey"
}
if ((Test-Path -LiteralPath $startMenu) -or (Test-Path -LiteralPath $deskflowStartMenu)) {
  throw 'Refusing to overwrite a pre-existing Start menu folder.'
}

try {
  New-Item -ItemType Directory -Path (Join-Path $installDir 'translations') -Force | Out-Null
  foreach ($path in $legacyInstallFiles) {
    New-Item -ItemType File -Path $path -Force | Out-Null
  }
  New-Item -ItemType Directory -Path $startMenu -Force | Out-Null
  New-Item -ItemType Directory -Path $deskflowStartMenu -Force | Out-Null
  foreach ($path in $legacyShortcuts) {
    Set-Content -LiteralPath $path -Value 'legacy Deskflow shortcut' -NoNewline
  }
  Set-Content -LiteralPath $userShortcut -Value 'keep user shortcut' -NoNewline
  Set-Content -LiteralPath $customFile -Value $sentinel -NoNewline

  New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
  if ($createdProfileFile) {
    Set-Content -LiteralPath $profileFile -Value $sentinel -NoNewline
  } else {
    $existingProfileHash = (Get-FileHash -LiteralPath $profileFile -Algorithm SHA256).Hash
  }
  Set-Content -LiteralPath $profileSentinel -Value $sentinel -NoNewline

  $setupProcess = Start-Process -FilePath $SetupPath -ArgumentList '/S' -Wait -PassThru
  if ($setupProcess.ExitCode -ne 0) { throw "Setup failed with exit code $($setupProcess.ExitCode)" }
  foreach ($path in @(
    (Join-Path $installDir 'monosync.exe'),
    (Join-Path $installDir 'mono-sync-core.exe'),
    (Join-Path $installDir 'Uninstall Mono Sync.exe'),
    (Join-Path $startMenu 'Mono Sync.lnk'),
    (Join-Path $startMenu 'Uninstall Mono Sync.lnk')
  )) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Setup did not create $path" }
  }
  if (-not (Test-Path -LiteralPath $uninstallKey)) {
    throw 'Setup did not create its current-user uninstall registration.'
  }
  foreach ($path in $legacyInstallFiles + $legacyShortcuts) {
    if (Test-Path -LiteralPath $path) { throw "Setup left a legacy Deskflow item behind: $path" }
  }
  if (-not (Test-Path -LiteralPath $userShortcut)) {
    throw 'Setup removed an unrecognized user shortcut.'
  }
  if ((Get-Content -LiteralPath $customFile -Raw) -ne $sentinel) {
    throw 'Setup changed an unrecognized file in the install folder.'
  }

  $managedFileHashes = @{}
  Get-ChildItem -LiteralPath $installDir -File -Recurse | ForEach-Object {
    $relativePath = $_.FullName.Substring($installDir.Length + 1)
    $managedFileHashes[$relativePath] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
  }
  $lockedPayload = Join-Path $installDir 'Qt6Core.dll'
  if (-not (Test-Path -LiteralPath $lockedPayload)) { throw "Expected locked-file fixture is missing: $lockedPayload" }
  $lockedFileHandle = [IO.File]::Open(
    $lockedPayload,
    [IO.FileMode]::Open,
    [IO.FileAccess]::Read,
    [IO.FileShare]::Read
  )
  try {
    $setupProcess = Start-Process -FilePath $SetupPath -ArgumentList '/S' -Wait -PassThru
    if ($setupProcess.ExitCode -eq 0) { throw 'Setup updated files while Qt6Core.dll was locked.' }

    $afterBlockedUpdate = @{}
    Get-ChildItem -LiteralPath $installDir -File -Recurse | ForEach-Object {
      $relativePath = $_.FullName.Substring($installDir.Length + 1)
      $afterBlockedUpdate[$relativePath] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    if ($managedFileHashes.Count -ne $afterBlockedUpdate.Count) {
      throw 'Setup changed the installed file set before rejecting a locked payload.'
    }
    foreach ($relativePath in $managedFileHashes.Keys) {
      if ($afterBlockedUpdate[$relativePath] -ne $managedFileHashes[$relativePath]) {
        throw "Setup changed $relativePath before rejecting a locked payload."
      }
    }
  }
  finally {
    $lockedFileHandle.Dispose()
  }

  $setupProcess = Start-Process -FilePath $SetupPath -ArgumentList '/S' -Wait -PassThru
  if ($setupProcess.ExitCode -ne 0) { throw "Setup update failed with exit code $($setupProcess.ExitCode)" }
  if (Test-Path -LiteralPath $profileFile) {
    if ($createdProfileFile) {
      if ((Get-Content -LiteralPath $profileFile -Raw) -ne $sentinel) {
        throw 'Setup update changed the test-created profile file.'
      }
    } elseif ((Get-FileHash -LiteralPath $profileFile -Algorithm SHA256).Hash -ne $existingProfileHash) {
      throw 'Setup update changed the existing per-user profile file.'
    }
  } else { throw 'Setup update removed the per-user profile file.' }
  if ((Get-Content -LiteralPath $profileSentinel -Raw) -ne $sentinel) {
    throw 'Setup update changed an unrecognized per-user profile file.'
  }
  if ((Get-Content -LiteralPath $customFile -Raw) -ne $sentinel) {
    throw 'Setup update changed a user-owned file in the install directory.'
  }

  $uninstaller = Join-Path $installDir 'Uninstall Mono Sync.exe'
  $uninstallProcess = Start-Process -FilePath $uninstaller -ArgumentList '/S' -Wait -PassThru
  if ($uninstallProcess.ExitCode -ne 0) { throw "Uninstall failed with exit code $($uninstallProcess.ExitCode)" }
  $deadline = (Get-Date).AddSeconds(15)
  while ((Test-Path -LiteralPath $uninstaller) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
  }
  if (Test-Path -LiteralPath (Join-Path $installDir 'monosync.exe')) {
    throw 'Uninstaller left a packaged executable behind.'
  }
  if (Test-Path -LiteralPath $uninstaller) { throw 'Uninstaller left its own executable behind.' }
  if ((Get-Content -LiteralPath $customFile -Raw) -ne $sentinel) {
    throw 'Uninstaller removed or changed a user-owned install-folder file.'
  }
  foreach ($shortcut in @(
    (Join-Path $startMenu 'Mono Sync.lnk'),
    (Join-Path $startMenu 'Uninstall Mono Sync.lnk')
  )) {
    if (Test-Path -LiteralPath $shortcut) { throw "Uninstaller left its shortcut behind: $shortcut" }
  }
  if (-not (Test-Path -LiteralPath $userShortcut)) {
    throw 'Uninstaller removed an unrecognized user shortcut.'
  }
  if (Test-Path -LiteralPath $uninstallKey) { throw 'Uninstaller left its current-user registry entry.' }
  if (Test-Path -LiteralPath $profileFile) {
    if ($createdProfileFile) {
      if ((Get-Content -LiteralPath $profileFile -Raw) -ne $sentinel) {
        throw 'Uninstaller changed or removed the test-created profile file.'
      }
    } elseif ((Get-FileHash -LiteralPath $profileFile -Algorithm SHA256).Hash -ne $existingProfileHash) {
      throw 'Uninstaller changed or removed the existing per-user profile file.'
    }
  } else { throw 'Uninstaller removed the per-user profile file.' }
  if ((Get-Content -LiteralPath $profileSentinel -Raw) -ne $sentinel) {
    throw 'Uninstaller changed or removed an unrecognized per-user profile file.'
  }
  Write-Output 'Setup, locked-file preflight, legacy cleanup, update, and uninstall passed; user profile and unrecognized files/shortcuts were preserved.'
}
finally {
  if (Test-Path -LiteralPath (Join-Path $installDir 'Uninstall Mono Sync.exe')) {
    $uninstaller = Join-Path $installDir 'Uninstall Mono Sync.exe'
    $uninstallProcess = Start-Process -FilePath $uninstaller -ArgumentList '/S' -Wait -PassThru
    if ($uninstallProcess.ExitCode -ne 0) {
      Write-Warning "Cleanup uninstaller exited with code $($uninstallProcess.ExitCode)."
    }
  }
  foreach ($path in $legacyInstallFiles + $legacyShortcuts + @($userShortcut, $customFile)) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
  }
  if ($createdProfileFile -and (Test-Path -LiteralPath $profileFile)) {
    Remove-Item -LiteralPath $profileFile -Force
  }
  if (Test-Path -LiteralPath $profileSentinel) {
    Remove-Item -LiteralPath $profileSentinel -Force
  }
  if ($createdProfile -and (Test-Path -LiteralPath $profileDir)) {
    $remainingProfileFiles = @(Get-ChildItem -LiteralPath $profileDir -Force)
    if ($remainingProfileFiles.Count -eq 0) { Remove-Item -LiteralPath $profileDir -Force }
  }
  if (Test-Path -LiteralPath $customFile) { Remove-Item -LiteralPath $customFile -Force }
  if (Test-Path -LiteralPath $installDir) {
    $remaining = @(Get-ChildItem -LiteralPath $installDir -Force)
    if ($remaining.Count -eq 0) { Remove-Item -LiteralPath $installDir -Force }
  }
  foreach ($folder in @($startMenu, $deskflowStartMenu)) {
    if (Test-Path -LiteralPath $folder) {
      $remaining = @(Get-ChildItem -LiteralPath $folder -Force)
      if ($remaining.Count -eq 0) { Remove-Item -LiteralPath $folder -Force }
    }
  }
}
