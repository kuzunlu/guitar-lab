; Installer for Guitar Lab, made with Inno Setup 6 (https://jrsoftware.org/isinfo.php).
; CI builds it after the plugin:
;   ISCC /DAppVersion=0.1.0 installer\GuitarLab.iss
; It installs the VST3 into the standard VST3 folder, and the standalone app into
; Program Files with a Start menu shortcut.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#ifndef BuildDir
  #define BuildDir "..\build\GuitarLab_artefacts\Release"
#endif

[Setup]
; Never change AppId: Windows uses it to recognise updates of the same program.
AppId={{EC7CA7EC-AD0F-4735-B147-70B70766C9E9}
AppName=Guitar Lab
AppVersion={#AppVersion}
AppPublisher=Kursat
DefaultDirName={autopf}\Guitar Lab
DisableDirPage=yes
DefaultGroupName=Guitar Lab
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\build\installer
OutputBaseFilename=Guitar-Lab-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
UninstallDisplayName=Guitar Lab
UninstallDisplayIcon={app}\Guitar Lab.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone app"; Flags: unchecked

[Files]
Source: "{#BuildDir}\VST3\Guitar Lab.vst3\*"; DestDir: "{commoncf64}\VST3\Guitar Lab.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\Standalone\Guitar Lab.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Guitar Lab"; Filename: "{app}\Guitar Lab.exe"
Name: "{autodesktop}\Guitar Lab"; Filename: "{app}\Guitar Lab.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Guitar Lab.exe"; Description: "Start the Guitar Lab standalone app"; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Guitar Lab.vst3"
