!include "MUI2.nsh"
!include "Util.nsh"

Unicode true
RequestExecutionLevel user
ManifestDPIAware true
AllowSkipFiles off
FileErrorText "Mono Sync could not replace an installed file. Close Mono Sync and retry, or cancel setup." "Mono Sync could not safely replace an installed file. Close Mono Sync and retry, or cancel setup."
Name "Mono Sync"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Mono Sync"
SetCompressor /SOLID lzma
CRCCheck force

VIProductVersion "0.2.3.0"
VIAddVersionKey "ProductName" "Mono Sync"
VIAddVersionKey "FileDescription" "Mono Sync per-user setup"
VIAddVersionKey "ProductVersion" "0.2.3"
VIAddVersionKey "FileVersion" "0.2.3"
VIAddVersionKey "LegalCopyright" "Mono Sync contributors"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mono Sync"
!define UNINSTALLER "Uninstall Mono Sync.exe"

!define MUI_ABORTWARNING
!define MUI_LICENSEPAGE_TEXT_TOP "Install Mono Sync for the current Windows account. This setup does not require administrator access."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PAYLOAD_DIR}\LICENSE"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var PreflightBlocked
Var PreflightFailure

!macro ProbeInstalledFile PATH
  Push "${PATH}"
  Call ProbeInstalledFile
!macroend

Section "Install"
  SetShellVarContext current
preflight_retry:
  StrCpy $PreflightBlocked ""
  StrCpy $PreflightFailure ""
  !include "${PREFLIGHT_FILE_LIST}"
  StrCmp $PreflightFailure "" preflight_check_lock
  IfSilent preflight_silent_failure
  MessageBox MB_ICONSTOP|MB_OK "Setup could not safely inspect $PreflightFailure. No installed files were changed. Check the file and run setup again."
  SetErrorLevel 11
  Abort
preflight_silent_failure:
  SetErrorLevel 11
  Abort
preflight_check_lock:
  StrCmp $PreflightBlocked "" preflight_done
  IfSilent preflight_silent_locked
  MessageBox MB_ICONEXCLAMATION|MB_RETRYCANCEL "Close Mono Sync, then choose Retry. Setup has not changed any installed files. The locked file is:$\r$\n$PreflightBlocked" IDRETRY preflight_retry
  SetErrorLevel 10
  Abort
preflight_silent_locked:
  SetErrorLevel 10
  Abort
preflight_done:
  SetOverwrite on
  SetOutPath "$TEMP"
  !include "${LEGACY_FILE_LIST}"
  RMDir "$INSTDIR\translations"
  RMDir "$SMPROGRAMS\Deskflow"
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD_DIR}\*.*"
  WriteUninstaller "$INSTDIR\${UNINSTALLER}"

  CreateDirectory "$SMPROGRAMS\Mono Sync"
  CreateShortcut "$SMPROGRAMS\Mono Sync\Mono Sync.lnk" "$INSTDIR\monosync.exe"
  CreateShortcut "$SMPROGRAMS\Mono Sync\Uninstall Mono Sync.lnk" "$INSTDIR\${UNINSTALLER}"

  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "Mono Sync"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "0.2.3"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Mono Sync"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\monosync.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\${UNINSTALLER}$\""
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Function RemoveLegacyFile
  Exch $0
  IfFileExists "$0" remove_legacy_file
  Goto remove_legacy_done
remove_legacy_file:
  ClearErrors
  Delete "$0"
  IfErrors remove_legacy_failed
  Goto remove_legacy_done
remove_legacy_failed:
  MessageBox MB_ICONSTOP|MB_OK "Close any running Deskflow or Mono Sync windows, then run setup again. No administrator access is needed."
  Abort
remove_legacy_done:
  Pop $0
FunctionEnd

Function ProbeInstalledFile
  Exch $0
  Push $1
  Push $2
  Push $3
  StrCmp $PreflightBlocked "" 0 probe_installed_done
  StrCmp $PreflightFailure "" 0 probe_installed_done
  StrCpy $3 "$0"
  IfFileExists "$3" 0 probe_installed_done
  System::Call 'kernel32::CreateFileW(w "$3", i 0x40000000, i 0, p 0, i 3, i 0x80, p 0) p.r1 ?e'
  Pop $2
  IntPtrCmp $1 -1 probe_installed_open_failed probe_installed_open probe_installed_open
probe_installed_open:
  System::Call 'kernel32::CloseHandle(p r1)'
  Goto probe_installed_done
probe_installed_open_failed:
  StrCmp $2 "32" probe_installed_locked
  StrCmp $2 "33" probe_installed_locked
  StrCpy $PreflightFailure "$3 (Windows error $2)"
  Goto probe_installed_done
probe_installed_locked:
  StrCpy $PreflightBlocked "$3"
probe_installed_done:
  Pop $3
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

Section "Uninstall"
  SetShellVarContext current
  SetOutPath "$TEMP"
  Delete "$SMPROGRAMS\Mono Sync\Mono Sync.lnk"
  Delete "$SMPROGRAMS\Mono Sync\Uninstall Mono Sync.lnk"
  RMDir "$SMPROGRAMS\Mono Sync"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  !include "${UNINSTALL_FILE_LIST}"
  Delete "$INSTDIR\${UNINSTALLER}"
  RMDir "$INSTDIR"
SectionEnd
