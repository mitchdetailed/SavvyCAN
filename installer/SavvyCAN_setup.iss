; SavvyCAN Windows installer
; Built against Qt 6.7.2 / MinGW 64-bit.
;
; Don't compile this by hand - run installer\build_installer.ps1 from the repo
; root. That script builds the release binary, stages deploy\ with windeployqt,
; compiles the translations and then invokes this script. Compiling on its own
; will fail unless deploy\ has already been staged that way.

#define MyAppName      "SavvyCAN"
#define MyAppVersion   "222"
#define MyAppPublisher "SavvyCAN Project"
#define MyAppURL       "https://github.com/mitchdetailed/SavvyCAN"
#define MyAppExeName   "SavvyCAN.exe"
#define DeployDir      "..\deploy"

; Fail early with a readable message rather than emitting a broken installer
#if !FileExists(AddBackslash(SourcePath) + DeployDir + "\" + MyAppExeName)
  #error deploy\SavvyCAN.exe not found - run installer\build_installer.ps1 first
#endif

[Setup]
AppId={{F3A2B5C1-8D4E-4F7A-9B2C-1E3D5F6A7B8C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} V{#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
VersionInfoVersion=1.0.{#MyAppVersion}.0
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=.
OutputBaseFilename=SavvyCAN_Setup_V{#MyAppVersion}_x64
SetupIconFile=..\icons\SavvyIcon.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
MinVersion=10.0.17763
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile=..\LICENSE
; Installing into Program Files needs elevation
PrivilegesRequired=admin
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The whole staged tree in one entry - the payload is whatever windeployqt and
; build_installer.ps1 produced, so this cannot drift out of sync the way an
; explicit per-DLL list does. Covers SavvyCAN.exe, the Qt libraries, the MinGW
; runtime, libusb-1.0.dll, the plugin folders, help\ and translations\.
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}";                       Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";                 Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Qt writes a few things next to the binary at runtime; clear the folder so an
; uninstall doesn't leave an empty tree behind. User settings live in
; %APPDATA%\EVTV and are deliberately kept - see the prompt in [Code].
Type: filesandordirs; Name: "{app}"

[Code]
// Offer to remove the INI settings SavvyCAN writes to %APPDATA%\EVTV
// (QSettings IniFormat, organisation "EVTV"). Keeping them by default means a
// reinstall or upgrade preserves the user's connections and window layout.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  SettingsDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    SettingsDir := ExpandConstant('{userappdata}\EVTV');
    if DirExists(SettingsDir) then
    begin
      if MsgBox('Also remove your SavvyCAN settings (saved connections, window positions)?' + #13#10 +
                'Choose No to keep them for a future reinstall.',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(SettingsDir, True, True, True);
    end;
  end;
end;
