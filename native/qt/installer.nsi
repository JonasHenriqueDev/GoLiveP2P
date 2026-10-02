Unicode true
!include "MUI2.nsh"
!define MUI_ICON "assets\golive-icon.ico"
!define MUI_UNICON "assets\golive-icon.ico"
Name "GoLive P2P"
OutFile "..\..\release\GoLive-P2P-Setup-0.6.0-qt.4.exe"
InstallDir "$LOCALAPPDATA\Programs\golive-p2p-qt"
InstallDirRegKey HKCU "Software\GoLiveP2P\Qt" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SilentInstall normal
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "PortugueseBR"
Function .onInit
  ; Allow updater to wait for the old Qt process to exit before replacement.
  Sleep 2500
FunctionEnd
Section "GoLive P2P"
  SetOutPath "$INSTDIR"
  File /r "..\..\release\qt-unpacked\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\GoLiveP2P\Qt" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GoLiveP2PQt" "DisplayName" "GoLive P2P (Qt)"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GoLiveP2PQt" "DisplayVersion" "0.6.0-qt.4"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GoLiveP2PQt" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  CreateShortcut "$DESKTOP\GoLive P2P.lnk" "$INSTDIR\GoLive P2P.exe"
  CreateDirectory "$SMPROGRAMS\GoLive P2P"
  CreateShortcut "$SMPROGRAMS\GoLive P2P\GoLive P2P.lnk" "$INSTDIR\GoLive P2P.exe"
  CreateShortcut "$SMPROGRAMS\GoLive P2P\Desinstalar.lnk" "$INSTDIR\Uninstall.exe"
  IfSilent 0 +2
    Exec '"$INSTDIR\GoLive P2P.exe"'
SectionEnd
Section "Uninstall"
  Delete "$DESKTOP\GoLive P2P.lnk"
  Delete "$SMPROGRAMS\GoLive P2P\GoLive P2P.lnk"
  Delete "$SMPROGRAMS\GoLive P2P\Desinstalar.lnk"
  RMDir "$SMPROGRAMS\GoLive P2P"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GoLiveP2PQt"
  DeleteRegKey HKCU "Software\GoLiveP2P\Qt"
  RMDir /r "$INSTDIR"
SectionEnd
