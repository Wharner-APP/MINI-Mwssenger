; MINI Messenger installer (Inno Setup 6).
; Build dist first: scripts\build.ps1 -Configuration Release
; Then run: iscc packaging\mini.iss

#define AppName "MINI Messenger"
#define AppVersion "0.1.0"
#define AppExe "MINI Messenger.exe"

[Setup]
AppId={{7D9F3B1E-2A64-4C7B-9E51-6B2C9A0E4F11}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=MINI Messenger contributors
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=out
OutputBaseFilename=MINI-Messenger-{#AppVersion}-win64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; GroupDescription: "Дополнительно:"

[Run]
Filename: "{app}\{#AppExe}"; Description: "Запустить {#AppName}"; Flags: nowait postinstall skipifsilent
