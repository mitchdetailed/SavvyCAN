; SavvyCAN Installer Script
; Built with Qt 6.7.2 / MinGW 11.2 64-bit
; Run from the project root after building:
;   1. Build release:  qmake SavvyCAN.pro CONFIG+=release && mingw32-make -j8
;   2. Run windeployqt to populate the deploy\ folder (already done)
;   3. Open this script in Inno Setup and compile

#define MyAppName      "SavvyCAN"
#define MyAppVersion   "1.0.0"
#define MyAppPublisher "SavvyCAN Project"
#define MyAppURL       "https://github.com/collin80/SavvyCAN"
#define MyAppExeName   "SavvyCAN.exe"
#define DeployDir      "..\deploy"

[Setup]
AppId={{F3A2B5C1-8D4E-4F7A-9B2C-1E3D5F6A7B8C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=.
OutputBaseFilename=SavvyCAN_Setup_{#MyAppVersion}_x64
SetupIconFile=..\icons\SavvyIcon.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
MinVersion=10.0.17763
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile=..\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main executable
Source: "{#DeployDir}\SavvyCAN.exe";        DestDir: "{app}"; Flags: ignoreversion

; Qt core DLLs
Source: "{#DeployDir}\Qt6Core.dll";          DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6Gui.dll";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6Network.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6OpenGL.dll";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6Pdf.dll";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6PrintSupport.dll";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6Qml.dll";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6QmlModels.dll";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6Quick.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6Quick3DUtils.dll";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6SerialBus.dll";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6SerialPort.dll";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6Svg.dll";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6VirtualKeyboard.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\Qt6Widgets.dll";       DestDir: "{app}"; Flags: ignoreversion

; DirectX / OpenGL support
Source: "{#DeployDir}\D3Dcompiler_47.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\opengl32sw.dll";       DestDir: "{app}"; Flags: ignoreversion

; MinGW 11.2 runtime DLLs
Source: "{#DeployDir}\libgcc_s_seh-1.dll";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\libstdc++-6.dll";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#DeployDir}\libwinpthread-1.dll"; DestDir: "{app}"; Flags: ignoreversion

; CAN bus plugins
Source: "{#DeployDir}\canbus\*"; DestDir: "{app}\canbus"; Flags: ignoreversion recursesubdirs

; Qt platform plugin (mandatory)
Source: "{#DeployDir}\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs

; Image format plugins
Source: "{#DeployDir}\imageformats\*"; DestDir: "{app}\imageformats"; Flags: ignoreversion recursesubdirs

; Icon engine plugins
Source: "{#DeployDir}\iconengines\*"; DestDir: "{app}\iconengines"; Flags: ignoreversion recursesubdirs

; Style plugins
Source: "{#DeployDir}\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs

; TLS / SSL backend plugins
Source: "{#DeployDir}\tls\*"; DestDir: "{app}\tls"; Flags: ignoreversion recursesubdirs

; Network information plugins
Source: "{#DeployDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs

; Virtual keyboard / input context plugins
Source: "{#DeployDir}\platforminputcontexts\*"; DestDir: "{app}\platforminputcontexts"; Flags: ignoreversion recursesubdirs

; Generic input plugins
Source: "{#DeployDir}\generic\*"; DestDir: "{app}\generic"; Flags: ignoreversion recursesubdirs

; QML debugger plugins (optional but included by windeployqt)
Source: "{#DeployDir}\qmltooling\*"; DestDir: "{app}\qmltooling"; Flags: ignoreversion recursesubdirs

; Qt translations
Source: "{#DeployDir}\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#MyAppName}";                   Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";             Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
