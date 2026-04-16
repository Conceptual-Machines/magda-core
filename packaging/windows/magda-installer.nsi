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
    File "MAGDA.exe"
    File /nonfatal "magda_plugin_scanner.exe"

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

    ; Notify shell of file association change
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\MAGDA.exe"
    Delete "$INSTDIR\magda_plugin_scanner.exe"
    Delete "$INSTDIR\Uninstall.exe"
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
