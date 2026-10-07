; notepad-mint.nsi - the installer of "notepad mint" (NSIS 3, unicode, modern ui).
; build it with tools\installer.bat (it passes the paths and the version), or by hand:
;   makensis /DVERSION=1.0.0 /DEXE_PATH=D:\...\build\notepad-mint.exe /DOUT_PATH=D:\...\build\notepad-mint-1.0.0-setup.exe /DICON_PATH=D:\...\assets\notepad_mint.ico installer\notepad-mint.nsi
; what it does: copies the one exe into program files, adds a start menu shortcut (and an optional desktop one), writes the uninstaller and its entry in
; "installed apps". the settings (%appdata%\notepad mint\settings.ini) are the user's own: the uninstaller leaves them alone.

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef EXE_PATH
  !error "EXE_PATH is not set (the built notepad-mint.exe)"
!endif
!ifndef OUT_PATH
  !define OUT_PATH "notepad-mint-${VERSION}-setup.exe"
!endif

!define APP_NAME   "notepad mint"
!define APP_EXE    "notepad-mint.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\notepad mint"

Name "${APP_NAME} ${VERSION}"
OutFile "${OUT_PATH}"
InstallDir "$PROGRAMFILES32\${APP_NAME}"
InstallDirRegKey HKLM "Software\${APP_NAME}" "InstallDir"
RequestExecutionLevel admin
BrandingText "${APP_NAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "FileDescription" "${APP_NAME} setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "free software. no ai, no sign-in, no telemetry."

!include "MUI2.nsh"
!include "x64.nsh"

!ifdef ICON_PATH
  !define MUI_ICON "${ICON_PATH}"
  !define MUI_UNICON "${ICON_PATH}"
!endif
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "run ${APP_NAME}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "${APP_NAME}" SecApp
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${EXE_PATH}"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKLM "Software\${APP_NAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "yuru"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  SectionGetSize ${SecApp} $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Section /o "desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

LangString DESC_App ${LANG_ENGLISH} "the editor (one small exe) and a start menu shortcut."
LangString DESC_Desktop ${LANG_ENGLISH} "a shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} $(DESC_App)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APP_NAME}.lnk"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\${APP_NAME}"
SectionEnd
