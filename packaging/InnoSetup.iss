;------------------------------------------------------------
; RoboticsStudio – Inno Setup script
;------------------------------------------------------------
#define MyAppName        "KRStudio"
#define MyAppVersion     "0.6.2"               ; <<<EDIT>>> or fill in with CMake using configure_file
#define MyAppPublisher   "Kurtz Robotics"
#define MyAppURL         "https://github.com/<YOU>/RoboticsStudio"
#define MyAppExeName     "KRStudio.exe" ; built file name

[Setup]
AppId={{A1B2C3D4-1111-2222-3333-444455556666}} ; <<<EDIT>>> GUID once, then keep
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=dist\installer
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-Setup
Compression=lzma
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; ---- Everything windeployqt produced ----
; The path is now corrected to look in the project's root build folder, not inside the packaging folder.
Source: "..\build\Release\*";      DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

; Optional: add README, sample scenes, etc.
Source: "..\README.md";           DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
