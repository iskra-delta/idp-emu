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
SetupIconFile=..\..\assets\icons\partner.ico
UninstallDisplayIcon={app}\partner.exe
ChangesEnvironment=no

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; v0.0.2 accidentally installed copies of the build runner's Windows system
; DLLs. Remove those and obsolete pre-flat layouts before installing the clean
; static build. Writable media lives under the user's application-data folder.
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\idp-emu.exe"
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\shared"

[Icons]
Name: "{autoprograms}\partnerg"; Filename: "{app}\partner.exe"; Parameters: "--model gdp --system-hdd"; WorkingDir: "{app}"
Name: "{autoprograms}\partner"; Filename: "{app}\partner.exe"; Parameters: "--model crt --system-floppy"; WorkingDir: "{app}"
Name: "{autodesktop}\partnerg"; Filename: "{app}\partner.exe"; Parameters: "--model gdp --system-hdd"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{autodesktop}\partner"; Filename: "{app}\partner.exe"; Parameters: "--model crt --system-floppy"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create &desktop shortcuts"; GroupDescription: "Additional icons:"; Flags: unchecked

[Run]
Filename: "{app}\partner.exe"; Parameters: "--model crt --system-floppy"; Description: "Launch partner"; Flags: nowait postinstall skipifsilent
