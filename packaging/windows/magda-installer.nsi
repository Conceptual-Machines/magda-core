!include "MUI2.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif

Name "MAGDA"
OutFile "MAGDA-${VERSION}-Windows-Setup.exe"
InstallDir "$PROGRAMFILES64\MAGDA"
InstallDirRegKey HKLM "Software\MAGDA" "Install_Dir"
RequestExecutionLevel admin
Unicode True

; UI settings
!define MUI_ABORTWARNING

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath $INSTDIR
    ; ${__FILEDIR__} resolves to the directory of this .nsi at compile time, so
    ; the installer builds correctly regardless of makensis's working directory.
    File "${__FILEDIR__}\MAGDA.exe"
    File "${__FILEDIR__}\mgd_doc_icon.ico"
    File /nonfatal "${__FILEDIR__}\magda_plugin_scanner.exe"

    ; All runtime DLLs staged next to MAGDA.exe: ONNX Runtime (delay-loaded by
    ; the media DB sample tagger) and libxml2 + its transitive deps (DAWproject
    ; XSD validation). Windows only searches the exe's directory, so these must
    ; sit next to MAGDA.exe or the dependent call faults. The release staging
    ; step asserts the critical DLLs are present, so this fails loudly upstream
    ; if one goes missing.
    File "${__FILEDIR__}\*.dll"

    ; Localization JSON files - StringTable looks for them next to MAGDA.exe
    SetOutPath "$INSTDIR\lang"
    File /r "${__FILEDIR__}\lang\*.*"
    SetOutPath $INSTDIR

    ; Controller profiles + Lua scripts - registry probes
    ; <exe>/controllers/profiles and LuaScriptStore probes
    ; <exe>/controllers/scripts. File /r preserves both subdirs.
    SetOutPath "$INSTDIR\controllers"
    File /r "${__FILEDIR__}\controllers\*.*"
    SetOutPath $INSTDIR

    ; Faust standard libraries - FaustResources passes <exe>/faustlibraries to
    ; libfaust as the -I include path, so import("stdfaust.lib") resolves. Uses
    ; \* (not \*.*) because the tree carries extensionless files (Makefile etc).
    SetOutPath "$INSTDIR\faustlibraries"
    File /r "${__FILEDIR__}\faustlibraries\*"
    SetOutPath $INSTDIR

    ; Stock drumkit templates - DrumkitManager reads <exe>/drumkits to seed the
    ; user's Drumkits folder on first launch.
    SetOutPath "$INSTDIR\drumkits"
    File /r "${__FILEDIR__}\drumkits\*"
    SetOutPath $INSTDIR

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Start Menu shortcuts
    CreateDirectory "$SMPROGRAMS\MAGDA"
    CreateShortcut "$SMPROGRAMS\MAGDA\MAGDA.lnk" "$INSTDIR\MAGDA.exe"
    CreateShortcut "$SMPROGRAMS\MAGDA\Uninstall MAGDA.lnk" "$INSTDIR\Uninstall.exe"

    ; Desktop shortcut
    CreateShortcut "$DESKTOP\MAGDA.lnk" "$INSTDIR\MAGDA.exe"

    ; Registry for Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MAGDA" \
        "DisplayName" "MAGDA"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MAGDA" \
        "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MAGDA" \
        "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MAGDA" \
        "Publisher" "Conceptual Machines"
    WriteRegStr HKLM "Software\MAGDA" "Install_Dir" "$INSTDIR"

    ; File association for .mgd files
    WriteRegStr HKCR ".mgd" "" "MAGDA.Project"
    WriteRegStr HKCR "MAGDA.Project" "" "MAGDA Project"
    WriteRegStr HKCR "MAGDA.Project\shell\open\command" "" '"$INSTDIR\MAGDA.exe" "%1"'
    WriteRegStr HKCR "MAGDA.Project\DefaultIcon" "" "$INSTDIR\mgd_doc_icon.ico"

    ; Notify shell of file association change
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\MAGDA.exe"
    Delete "$INSTDIR\mgd_doc_icon.ico"
    Delete "$INSTDIR\magda_plugin_scanner.exe"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir /r "$INSTDIR\lang"
    RMDir /r "$INSTDIR\controllers"
    RMDir /r "$INSTDIR\faustlibraries"
    RMDir /r "$INSTDIR\drumkits"
    RMDir "$INSTDIR"

    Delete "$SMPROGRAMS\MAGDA\*.*"
    RMDir "$SMPROGRAMS\MAGDA"
    Delete "$DESKTOP\MAGDA.lnk"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MAGDA"
    DeleteRegKey HKLM "Software\MAGDA"

    ; Remove file association
    DeleteRegKey HKCR ".mgd"
    DeleteRegKey HKCR "MAGDA.Project"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
