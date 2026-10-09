; notepad-mint.nsi - the installer of "notepad mint" (NSIS 3, unicode, modern ui).
; build it with tools\installer.bat (it passes the paths and the version), or by hand:
;   makensis /DVERSION=1.0.0 /DEXE_PATH=D:\...\build\notepad-mint.exe /DOUT_PATH=D:\...\build\notepad-mint-1.0.0-setup.exe /DICON_PATH=D:\...\assets\notepad_mint.ico installer\notepad-mint.nsi
; what it does: copies the one exe into program files, adds a start menu shortcut (and an optional desktop one), writes the uninstaller and its entry in
; "installed apps", and (component "text file types", on by default) registers notepad mint as a choice for the plain text extensions: "open with" and
; settings > default apps list it. windows lets only the user pick a default program, so the finish page offers to open that settings page.
; the settings (%appdata%\notepad-mint\settings.ini) are the user's own: the uninstaller leaves them alone.

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !define VERSION "1.0.11"
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
!define PROGID     "notepad-mint.text"                    ; the same names as src\mp.h (ASSOC_*) and src\assoc.c (the per-user copy of these keys)
!define REGAPP     "notepad-mint"
!define CAPS       "Software\notepad mint\Capabilities"

Name "${APP_NAME} ${VERSION}"
OutFile "${OUT_PATH}"
InstallDir "$PROGRAMFILES32\notepad-mint"
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
!include "Sections.nsh"

Var AssocBefore                                           ; 1 = an earlier install already registered the file types (an update: the finish page doesn't open settings again)

!ifdef ICON_PATH
  !define MUI_ICON "${ICON_PATH}"
  !define MUI_UNICON "${ICON_PATH}"
!endif
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "run ${APP_NAME}"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "choose ${APP_NAME} as the default for text files (opens windows settings)"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION OpenDefaultApps
!define MUI_PAGE_CUSTOMFUNCTION_SHOW FinishShow

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; an earlier installer used "program files (x86)\notepad mint" (with a space): take that copy away first (its own uninstaller, silently), so an update
; leaves one copy, in "notepad-mint"
Function .onInit
  StrCpy $AssocBefore 0
  ${If} ${RunningX64}
    SetRegView 64
  ${EndIf}
  ReadRegStr $0 HKLM "Software\RegisteredApplications" "${REGAPP}"
  SetRegView 32
  StrCmp $0 "" +2
    StrCpy $AssocBefore 1
  IfFileExists "$PROGRAMFILES32\notepad mint\uninstall.exe" 0 +3
    ExecWait '"$PROGRAMFILES32\notepad mint\uninstall.exe" /S _?=$PROGRAMFILES32\notepad mint'
    Delete "$PROGRAMFILES32\notepad mint\uninstall.exe"
  RMDir "$PROGRAMFILES32\notepad mint"
FunctionEnd

Section "${APP_NAME}" SecApp
  SectionIn RO
  SetOutPath "$INSTDIR"
  ClearErrors
  File "${EXE_PATH}"
  IfErrors 0 +3
    SetErrorLevel 1
    Abort
  WriteUninstaller "$INSTDIR\uninstall.exe"
  IfErrors 0 +3
    SetErrorLevel 1
    Abort
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

; one extension: an "open with" choice (the extension's own default is left alone), a supported type, a capability
!macro AssocExt EXT
  WriteRegStr HKLM "Software\Classes\${EXT}\OpenWithProgids" "${PROGID}" ""
  WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}\SupportedTypes" "${EXT}" ""
  WriteRegStr HKLM "${CAPS}\FileAssociations" "${EXT}" "${PROGID}"
!macroend
!macro UnAssocExt ROOT EXT
  DeleteRegValue ${ROOT} "Software\Classes\${EXT}\OpenWithProgids" "${PROGID}"   ; only our value: the key is shared with other programs (DeleteRegKey /ifempty ignores values)
!macroend
!macro AssocAll MACRO ARG
  !insertmacro ${MACRO} ${ARG} .txt
  !insertmacro ${MACRO} ${ARG} .log
  !insertmacro ${MACRO} ${ARG} .ini
  !insertmacro ${MACRO} ${ARG} .cfg
  !insertmacro ${MACRO} ${ARG} .conf
  !insertmacro ${MACRO} ${ARG} .md
  !insertmacro ${MACRO} ${ARG} .csv
  !insertmacro ${MACRO} ${ARG} .nfo
  !insertmacro ${MACRO} ${ARG} .diz
  !insertmacro ${MACRO} ${ARG} .text
!macroend
!macro UnAssoc ROOT
  !insertmacro AssocAll UnAssocExt ${ROOT}
  DeleteRegKey ${ROOT} "Software\Classes\${PROGID}"
  DeleteRegKey ${ROOT} "Software\Classes\Applications\${APP_EXE}"
  DeleteRegValue ${ROOT} "Software\RegisteredApplications" "${REGAPP}"
  DeleteRegKey ${ROOT} "Software\notepad mint\Capabilities"
  DeleteRegKey /ifempty ${ROOT} "Software\notepad mint"
!macroend
!macro AssocExtNoRoot DUMMY EXT
  !insertmacro AssocExt ${EXT}
!macroend

; the 64-bit registry view on 64-bit windows: that is where explorer and settings look for RegisteredApplications (a 32-bit installer writes to
; WOW6432Node otherwise). Software\Classes is shared by both views
Section "text file types" SecAssoc
  ${If} ${RunningX64}
    SetRegView 64
  ${EndIf}
  WriteRegStr HKLM "Software\Classes\${PROGID}" "" "text document"
  WriteRegExpandStr HKLM "Software\Classes\${PROGID}\DefaultIcon" "" "%SystemRoot%\system32\imageres.dll,-102"
  WriteRegStr HKLM "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'
  WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}" "FriendlyAppName" "${APP_NAME}"
  WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'
  WriteRegStr HKLM "${CAPS}" "ApplicationName" "${APP_NAME}"
  WriteRegStr HKLM "${CAPS}" "ApplicationDescription" "a small dark / light notepad"
  WriteRegStr HKLM "${CAPS}" "ApplicationIcon" "$INSTDIR\${APP_EXE},0"
  !insertmacro AssocAll AssocExtNoRoot x
  WriteRegStr HKLM "Software\RegisteredApplications" "${REGAPP}" "${CAPS}"
  SetRegView 32
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'   ; SHCNE_ASSOCCHANGED
SectionEnd

Function OpenDefaultApps
  ExecShell "open" "ms-settings:defaultapps?registeredAppMachine=${REGAPP}"
FunctionEnd

; the "default for text files" box: only with the file types, and not ticked again on an update that had them already
Function FinishShow
  ${IfNot} ${SectionIsSelected} ${SecAssoc}
    SendMessage $mui.FinishPage.ShowReadme ${BM_SETCHECK} 0 0
    ShowWindow $mui.FinishPage.ShowReadme ${SW_HIDE}
  ${ElseIf} $AssocBefore == 1
    SendMessage $mui.FinishPage.ShowReadme ${BM_SETCHECK} 0 0
  ${EndIf}
FunctionEnd

Section /o "desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

LangString DESC_App ${LANG_ENGLISH} "the editor (one small exe) and a start menu shortcut."
LangString DESC_Desktop ${LANG_ENGLISH} "a shortcut on the desktop."
LangString DESC_Assoc ${LANG_ENGLISH} "list ${APP_NAME} under $\"open with$\" and in settings > default apps for .txt .log .ini .cfg .conf .md .csv .nfo .diz .text (nothing changes until you pick it)."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} $(DESC_App)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desktop)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} $(DESC_Assoc)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APP_NAME}.lnk"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\${APP_NAME}"
  ${If} ${RunningX64}
    SetRegView 64
  ${EndIf}
  !insertmacro UnAssoc HKLM
  SetRegView 32
  !insertmacro UnAssoc HKCU                              ; (help > set as default text editor writes the same keys per user)
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
