#ifndef SourceDir
  #error SourceDir must point to the staged release tree
#endif
#ifndef AppVersion
  #error AppVersion must be supplied
#endif
#ifndef OutputDir
  #error OutputDir must be supplied
#endif

[Setup]
AppId={{24E76C43-F23B-4F68-A8E9-A73334043392}
AppName=Iskra Delta Partner Emulator
AppVersion={#AppVersion}
AppPublisher=Iskra Delta Emulator Project
DefaultDirName={autopf}\Iskra Delta Partner Emulator
DefaultGroupName=Iskra Delta Partner Emulator
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Iskra-Delta-Partner-{#AppVersion}-Windows-x86_64-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\idp-emu.exe
ChangesEnvironment=no

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Iskra Delta Partner Emulator"; Filename: "{app}\bin\idp-emu.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Iskra Delta Partner Emulator"; Filename: "{app}\bin\idp-emu.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Run]
Filename: "{app}\bin\idp-emu.exe"; Description: "Launch Iskra Delta Partner Emulator"; Flags: nowait postinstall skipifsilent
