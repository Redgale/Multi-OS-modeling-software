; SPDX-License-Identifier: GPL-3.0-or-later
; Windows installer for Jigsmith (Inno Setup 6). Built in CI from the portable folder:
;   ISCC.exe /DAppVersion=0.0.1 /DSourceDir=..\..\dist packaging\windows\jigsmith.iss
; Installs per user by default (no administrator rights needed); an administrator can
; choose to install for everyone. Adds a Start menu entry, opens .jig files, uninstalls.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist"
#endif

[Setup]
AppId={{6F1D2A4E-3B7C-4E8A-9C21-5A0B7D3E9F14}
AppName=Jigsmith
AppVersion={#AppVersion}
AppPublisher=The Jigsmith contributors
AppPublisherURL=https://github.com/Redgale/Multi-OS-modeling-software
DefaultDirName={autopf}\Jigsmith
DefaultGroupName=Jigsmith
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
LicenseFile={#SourceDir}\LICENSE
OutputDir=..\..
OutputBaseFilename=Jigsmith-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
UninstallDisplayIcon={app}\jigsmith.exe

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Jigsmith"; Filename: "{app}\jigsmith.exe"
Name: "{autodesktop}\Jigsmith"; Filename: "{app}\jigsmith.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Put a Jigsmith icon on the desktop"; Flags: unchecked
Name: "associate"; Description: "Open .jig design files with Jigsmith"

[Registry]
Root: HKA; Subkey: "Software\Classes\.jig"; ValueType: string; ValueName: ""; ValueData: "Jigsmith.Design"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Jigsmith.Design"; ValueType: string; ValueName: ""; ValueData: "Jigsmith design"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Jigsmith.Design\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\jigsmith.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Jigsmith.Design\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\jigsmith.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\jigsmith.exe"; Description: "Start Jigsmith"; Flags: nowait postinstall skipifsilent
