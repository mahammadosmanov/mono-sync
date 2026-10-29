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
$profileDir = Join-Path $env:APPDATA 'Deskflow'
$profileFile = Join-Path $profileDir 'Deskflow.conf'
$customFile = Join-Path $installDir 'user-owned-smoke-test.txt'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Mono Sync'
$uninstallKey = 'Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\Mono Sync'
$sentinel = 'setup-smoke-test-user-profile-preserved'
$createdProfile = -not (Test-Path -LiteralPath $profileDir)

if (Test-Path -LiteralPath $installDir) {
  throw "Refusing to overwrite a pre-existing test install: $installDir"
}
if (Test-Path -LiteralPath $profileFile) {
  throw "Refusing to overwrite pre-existing profile data: $profileFile"
}
if (Test-Path -LiteralPath $uninstallKey) {
  throw "Refusing to overwrite a pre-existing uninstall registration: $uninstallKey"
}

New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
Set-Content -LiteralPath $profileFile -Value $sentinel -NoNewline

try {
  & $SetupPath /S
  if ($LASTEXITCODE -ne 0) { throw "Setup failed with exit code $LASTEXITCODE" }
  foreach ($path in @(
    (Join-Path $installDir 'deskflow.exe'),
    (Join-Path $installDir 'deskflow-core.exe'),
    (Join-Path $installDir 'Uninstall Mono Sync.exe'),
    (Join-Path $startMenu 'Mono Sync.lnk'),
    (Join-Path $startMenu 'Uninstall Mono Sync.lnk')
  )) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Setup did not create $path" }
  }
  if (-not (Test-Path -LiteralPath $uninstallKey)) {
    throw 'Setup did not create its current-user uninstall registration.'
  }
  Set-Content -LiteralPath $customFile -Value $sentinel -NoNewline

  & $SetupPath /S
  if ($LASTEXITCODE -ne 0) { throw "Setup update failed with exit code $LASTEXITCODE" }
  if ((Get-Content -LiteralPath $profileFile -Raw) -ne $sentinel) {
    throw 'Setup update changed the existing per-user profile file.'
  }
  if ((Get-Content -LiteralPath $customFile -Raw) -ne $sentinel) {
    throw 'Setup update changed a user-owned file in the install directory.'
  }

  $uninstaller = Join-Path $installDir 'Uninstall Mono Sync.exe'
  & $uninstaller /S
  if ($LASTEXITCODE -ne 0) { throw "Uninstall failed with exit code $LASTEXITCODE" }
  $deadline = (Get-Date).AddSeconds(15)
  while ((Test-Path -LiteralPath $uninstaller) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
  }
  if (Test-Path -LiteralPath (Join-Path $installDir 'deskflow.exe')) {
    throw 'Uninstaller left a packaged executable behind.'
  }
  if (Test-Path -LiteralPath $uninstaller) { throw 'Uninstaller left its own executable behind.' }
  if ((Get-Content -LiteralPath $customFile -Raw) -ne $sentinel) {
    throw 'Uninstaller removed or changed a user-owned install-folder file.'
  }
  if (Test-Path -LiteralPath $startMenu) { throw 'Uninstaller left its Start menu folder behind.' }
  if (Test-Path -LiteralPath $uninstallKey) { throw 'Uninstaller left its current-user registry entry.' }
  if ((Get-Content -LiteralPath $profileFile -Raw) -ne $sentinel) {
    throw 'Uninstaller changed or removed the per-user profile file.'
  }
  Write-Output 'Setup, update, and uninstall passed; user profile and unrecognized install file were preserved.'
}
finally {
  if (Test-Path -LiteralPath (Join-Path $installDir 'Uninstall Mono Sync.exe')) {
    $uninstaller = Join-Path $installDir 'Uninstall Mono Sync.exe'
    & $uninstaller /S
  }
  if (Test-Path -LiteralPath $profileFile) { Remove-Item -LiteralPath $profileFile -Force }
  if ($createdProfile -and (Test-Path -LiteralPath $profileDir)) {
    Remove-Item -LiteralPath $profileDir -Force
  }
  if (Test-Path -LiteralPath $customFile) { Remove-Item -LiteralPath $customFile -Force }
  if (Test-Path -LiteralPath $installDir) {
    $remaining = @(Get-ChildItem -LiteralPath $installDir -Force)
    if ($remaining.Count -eq 0) { Remove-Item -LiteralPath $installDir -Force }
  }
}
