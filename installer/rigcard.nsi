; Rigcard installer: per-user (no admin), into %LOCALAPPDATA%\Programs\Rigcard (where Hangar expects it).
; Build: makensis installer/rigcard.nsi   (after ./build.sh; reads dist/Rigcard.exe)
Unicode true
!define VERSION "1.0.0"
!define VERSION_NUM "1.0.0.0"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Rigcard"
!include "MUI2.nsh"

Name "Rigcard"
OutFile "..\dist\Rigcard-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\Rigcard"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "Rigcard ${VERSION}"
VIProductVersion "${VERSION_NUM}"
VIAddVersionKey "ProductName" "Rigcard"
VIAddVersionKey "CompanyName" "larosa023"
VIAddVersionKey "FileDescription" "Rigcard installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "larosa023"

!define MUI_ICON "..\res\rigcard.ico"
!define MUI_UNICON "..\res\rigcard.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\Rigcard.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open Rigcard"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Rigcard (required)" SecMain
  SectionIn RO
  ; close a running copy the normal way first
  IfFileExists "$INSTDIR\Rigcard.exe" 0 +2
    ExecWait '"$INSTDIR\Rigcard.exe" --quit'
  SetOutPath "$INSTDIR"
  File "..\dist\Rigcard.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\Rigcard.lnk" "$INSTDIR\Rigcard.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "Rigcard"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "larosa023"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\Rigcard.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "https://github.com/larosa023/Rigcard"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" 2048
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortCut "$DESKTOP\Rigcard.lnk" "$INSTDIR\Rigcard.exe"
SectionEnd

LangString DESC_Main ${LANG_ENGLISH} "Rigcard itself (about 1.5 MB). It updates itself from GitHub."
LangString DESC_Desktop ${LANG_ENGLISH} "A shortcut on your desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_Main)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  ExecWait '"$INSTDIR\Rigcard.exe" --quit'
  nsExec::Exec 'schtasks.exe /Delete /TN "Rigcard Sensors" /F'
  Delete "$SMPROGRAMS\Rigcard.lnk"
  Delete "$DESKTOP\Rigcard.lnk"
  Delete "$INSTDIR\Rigcard.exe"
  Delete "$INSTDIR\Rigcard.exe.old"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINST_KEY}"
  IfSilent done
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also delete your Rigcard data (build, photo, benchmark history, settings)?" IDNO done
    RMDir /r "$LOCALAPPDATA\Rigcard"
  done:
SectionEnd
