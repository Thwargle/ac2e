[Setup]
AppId={{5D901DF4-69F3-4075-A1D9-65C9E624BCD6}
AppName=AC2:Unreal
AppVersion={#ReleaseVersion}
AppPublisher=Thwargle
AppPublisherURL=https://thwargle.github.io/ac2e/
AppSupportURL=https://github.com/Thwargle/ac2e/issues
DefaultDirName={localappdata}\Programs\AC2Unreal
DefaultGroupName=AC2 Unreal
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableProgramGroupPage=yes
WizardStyle=modern
OutputDir={#ReleaseOut}
OutputBaseFilename=AC2-Unreal-Setup-{#ReleaseVersion}
Compression=lzma2/fast
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
Uninstallable=not Portable
CreateUninstallRegKey=not Portable
UninstallDisplayIcon={app}\AC2Launcher.exe
VersionInfoVersion={#ReleaseVersion}.0

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: checkedonce

[Files]
Source: "{#Payload}\*"; Excludes: "*.pdb,*.debug,*.sym,*.target,*.modules,Manifest_*.txt,*CrashReportClient*,*FileOpenOrder*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs
Source: "{#Redist}\vc_redist.x64.exe"; DestDir: "{app}\Prerequisites"; Flags: ignoreversion
Source: "{#Redist}\GameInputRedist.msi"; DestDir: "{app}\Prerequisites"; Flags: ignoreversion

[Icons]
Name: "{group}\AC2 Unreal"; Check: not Portable; Filename: "{app}\AC2Launcher.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\AC2 Unreal"; Filename: "{app}\AC2Launcher.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Check: not Portable

[Run]
Filename: "{app}\Prerequisites\vc_redist.x64.exe"; Parameters: "/install /passive /norestart"; Description: "Install the Microsoft Visual C++ runtime"; Flags: shellexec postinstall skipifsilent waituntilterminated; Check: NeedsVC
Filename: "{sys}\msiexec.exe"; Parameters: "/i ""{app}\Prerequisites\GameInputRedist.msi"" /passive /norestart"; Description: "Install Microsoft GameInput"; Flags: postinstall skipifsilent waituntilterminated; Check: NeedsGameInput
Filename: "{app}\AC2Launcher.exe"; Description: "Open AC2:Unreal"; Flags: postinstall nowait skipifsilent
Filename: "{app}\AC2Launcher.exe"; Flags: nowait; Check: UpdateRestart

[Code]
function Portable: Boolean;
var I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do if CompareText(ParamStr(I), '/AC2PORTABLE') = 0 then Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and Portable then
    SaveStringToFile(ExpandConstant('{app}\portable.txt'), 'AC2:Unreal portable installation', False);
end;

function NeedsVC: Boolean;
var Installed: Cardinal;
begin
  Result := not RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) or (Installed <> 1);
end;
function NeedsGameInput: Boolean;
begin
  Result := not FileExists(ExpandConstant('{pf64}\Microsoft GameInput\x64\GameInputRedist.dll'));
end;
function UpdateRestart: Boolean;
var I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do if CompareText(ParamStr(I), '/AC2RESTART') = 0 then Result := True;
end;
