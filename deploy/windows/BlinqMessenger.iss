#define AppName "Blinq Messenger"
#define AppVersion "1.0.0"
#define AppPublisher "Exe Innovate"
#ifndef SourceDir
#define SourceDir "dist\BlinqMessenger"
#endif
#ifndef OutputDir
#define OutputDir "installer"
#endif

[Setup]
AppId={{8B7F2C4A-75BD-49DF-9D9B-19C5BE8F78C1}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\Blinq Messenger
DefaultGroupName=Blinq Messenger
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=BlinqMessengerSetup-{#AppVersion}
SetupIconFile=..\..\assets\appicon.ico
LicenseFile=license.txt
UninstallDisplayIcon={app}\messenger.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "Start Blinq Messenger when Windows starts"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Blinq Messenger"; Filename: "{app}\messenger.exe"; AppUserModelID: "BlinqMessenger"
Name: "{autodesktop}\Blinq Messenger"; Filename: "{app}\messenger.exe"; Tasks: desktopicon; AppUserModelID: "BlinqMessenger"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Blinq Messenger"; ValueData: """{app}\messenger.exe"" --startup"; Tasks: startup

[Run]
Filename: "{app}\messenger.exe"; Description: "Launch Blinq Messenger"; Flags: nowait postinstall skipifsilent
