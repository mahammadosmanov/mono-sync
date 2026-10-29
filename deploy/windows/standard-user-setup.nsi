!include "MUI2.nsh"

Unicode true
RequestExecutionLevel user
ManifestDPIAware true
Name "Mono Sync"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Mono Sync"
SetCompressor /SOLID lzma
CRCCheck force

VIProductVersion "0.2.0.0"
VIAddVersionKey "ProductName" "Mono Sync"
VIAddVersionKey "FileDescription" "Mono Sync per-user setup"
VIAddVersionKey "ProductVersion" "0.2.0"
VIAddVersionKey "FileVersion" "0.2.0"
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

Section "Install"
  SetShellVarContext current
  SetOverwrite on
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD_DIR}\*.*"
  WriteUninstaller "$INSTDIR\${UNINSTALLER}"

  CreateDirectory "$SMPROGRAMS\Mono Sync"
  CreateShortcut "$SMPROGRAMS\Mono Sync\Mono Sync.lnk" "$INSTDIR\monosync.exe"
  CreateShortcut "$SMPROGRAMS\Mono Sync\Uninstall Mono Sync.lnk" "$INSTDIR\${UNINSTALLER}"

  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "Mono Sync"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "0.2.0"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Mono Sync"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\monosync.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\${UNINSTALLER}$\""
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

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
