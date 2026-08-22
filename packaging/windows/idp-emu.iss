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
; Retire the old ambiguously named shortcuts when upgrading an installation.
Type: files; Name: "{autoprograms}\partner.lnk"
Type: files; Name: "{autoprograms}\partnerg.lnk"
Type: files; Name: "{autodesktop}\partner.lnk"
Type: files; Name: "{autodesktop}\partnerg.lnk"

[Icons]
Name: "{autoprograms}\partner-classic"; Filename: "{app}\partner-classic.bat"; WorkingDir: "{app}"; IconFilename: "{app}\partner.exe"
Name: "{autoprograms}\partner-graphical"; Filename: "{app}\partner-graphical.bat"; WorkingDir: "{app}"; IconFilename: "{app}\partner.exe"
Name: "{autodesktop}\partner-classic"; Filename: "{app}\partner-classic.bat"; WorkingDir: "{app}"; IconFilename: "{app}\partner.exe"; Tasks: desktopicon
Name: "{autodesktop}\partner-graphical"; Filename: "{app}\partner-graphical.bat"; WorkingDir: "{app}"; IconFilename: "{app}\partner.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create &desktop shortcuts"; GroupDescription: "Additional icons:"; Flags: unchecked

[Run]
Filename: "{app}\partner-classic.bat"; WorkingDir: "{app}"; Description: "Launch partner-classic"; Flags: nowait postinstall skipifsilent
