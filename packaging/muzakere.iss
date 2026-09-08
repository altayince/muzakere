#ifndef SourceDir
  #error SourceDir must be supplied by scripts/package.ps1
#endif
#ifndef OutputDir
  #error OutputDir must be supplied by scripts/package.ps1
#endif
[Setup]
AppId={{4A40F483-041B-44B8-8BC8-8EC7A2F3D7E2}
AppName=Müzakere
AppVersion=0.4.0
AppPublisher=Müzakere
DefaultDirName={localappdata}\Programs\Muzakere
DefaultGroupName=Müzakere
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\muzakere.exe
CloseApplications=yes
RestartApplications=no
[Languages]
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
[Tasks]
Name: "desktopicon"; Description: "Masaüstü kısayolu oluştur"; GroupDescription: "Kısayollar:"
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\Müzakere"; Filename: "{app}\muzakere.exe"
Name: "{autodesktop}\Müzakere"; Filename: "{app}\muzakere.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\muzakere.exe"; Description: "Müzakere'yi aç"; Flags: nowait postinstall skipifsilent
