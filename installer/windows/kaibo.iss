; Kaibo for OBS Studio — Windows installer (Inno Setup 6)
;
; Built by .github/scripts/Package-Windows.ps1:
;   iscc /DAppVersion=1.0.0 /DSourceDir=<release>\obs-kaibo /DOutputDir=<release> [/DHaveChinese] kaibo.iss
;
; What it owns, and nothing else:
;   {commonappdata}\obs-studio\plugins\obs-kaibo\        program files (removed on uninstall)
;   {userappdata}\obs-studio\plugin_config\obs-kaibo\    settings + sign-in (kept unless the user opts in)
; The AppId never changes, so a newer installer upgrades the existing install in place.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\release\RelWithDebInfo\obs-kaibo"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\release"
#endif

[Setup]
AppId={{6E0B7C1A-2F4D-4E8B-9A3C-5D7F1B2E4C60}
AppName=Kaibo for OBS Studio
AppVersion={#AppVersion}
AppVerName=Kaibo {#AppVersion}
AppPublisher=Kaibo contributors
AppPublisherURL=https://github.com/PZW152/obs-kaibo
AppSupportURL=https://github.com/PZW152/obs-kaibo/issues
DefaultDirName={commonappdata}\obs-studio\plugins\obs-kaibo
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
UsePreviousAppDir=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=obs-kaibo-{#AppVersion}-windows-x64-installer
SetupIconFile=kaibo.ico
UninstallDisplayIcon={app}\kaibo.ico
UninstallDisplayName=Kaibo for OBS Studio
UninstallFilesDir={app}\uninstall
LicenseFile=..\..\LICENSE
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
CloseApplications=no
ShowLanguageDialog=auto

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
#ifdef HaveChinese
Name: "zhcn"; MessagesFile: "ChineseSimplified.isl"
Name: "zhtw"; MessagesFile: "ChineseTraditional.isl"
#endif

[CustomMessages]
en.ObsRunning=OBS Studio is running. Close OBS, then click Retry.
en.RemoveLegacy=Remove the old "BILIBILI Stream for OBS" plugin (it adds a second Bilibili menu)
en.RemoveData=Also delete your Kaibo settings and saved Bilibili sign-in?%n%n%1%n%nChoose No to keep them for a future reinstall.
en.RemoveDataFailed=Some Kaibo data could not be deleted:%n%1
#ifdef HaveChinese
zhcn.ObsRunning=OBS Studio 正在运行。请先关闭 OBS，然后点「重试」。
zhcn.RemoveLegacy=移除旧版「BILIBILI Stream for OBS」插件（它会多出一个 B 站菜单）
zhcn.RemoveData=是否同时删除 Kaibo 的设置和已保存的 B 站登录凭证？%n%n%1%n%n选「否」会保留它们，以后重新安装可直接使用。
zhcn.RemoveDataFailed=部分 Kaibo 数据无法删除：%n%1
zhtw.ObsRunning=OBS Studio 正在執行。請先關閉 OBS，再按「重試」。
zhtw.RemoveLegacy=移除舊版「BILIBILI Stream for OBS」外掛（它會多出一個 B 站選單）
zhtw.RemoveData=是否一併刪除 Kaibo 的設定和已儲存的 B 站登入憑證？%n%n%1%n%n選「否」會保留它們，日後重新安裝可直接使用。
zhtw.RemoveDataFailed=部分 Kaibo 資料無法刪除：%n%1
#endif

[Tasks]
Name: "removelegacy"; Description: "{cm:RemoveLegacy}"; Flags: unchecked; Check: LegacyInstalled

[InstallDelete]
; Files from earlier Kaibo versions that this version no longer ships.
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\bin"

[Files]
Source: "{#SourceDir}\bin\64bit\obs-kaibo.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#SourceDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "kaibo.ico"; DestDir: "{app}"; Flags: ignoreversion

[Code]
const
  LegacyDir = '{commonappdata}\obs-studio\plugins\bilibili-stream-for-obs';

{ WMI can be disabled or broken (stripped images, security software). Report whether the
  service could answer at all, so the caller can use tasklist instead of silently
  treating "no answer" as "OBS is not running". }
function TryWmiObsRunning(var Running: Boolean): Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  Running := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('', 'root\CIMV2', '', '');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name="obs64.exe"');
    Running := Processes.Count > 0;
    Result := True;
  except
    Result := False;
  end;
end;

{ Fallback when WMI is unavailable: tasklist prints the image name only when the process
  exists, and findstr turns that into an exit code (0 = found), so no file or string
  helper is needed. The "no tasks" notice is localised and does not contain the name. }
function TasklistSaysObsRunning(): Boolean;
var
  ExitCode: Integer;
begin
  Result := False;
  if not Exec(ExpandConstant('{cmd}'),
              '/c tasklist /FI "IMAGENAME eq obs64.exe" /NH | findstr /I /C:"obs64.exe" >NUL',
              '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    Exit;
  Result := ExitCode = 0;
end;

function IsObsRunning(): Boolean;
var
  Running: Boolean;
begin
  if TryWmiObsRunning(Running) then
    Result := Running
  else
    Result := TasklistSaysObsRunning();
end;

{ Silent comes from the caller: UninstallSilent may only be called while
  uninstalling and WizardSilent only while installing (issue #2). }
function WaitForObsClosed(Silent: Boolean): Boolean;
begin
  Result := True;
  while IsObsRunning() do
  begin
    if Silent then
    begin
      Result := False;
      Exit;
    end;
    if MsgBox(CustomMessage('ObsRunning'), mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := WaitForObsClosed(WizardSilent);
end;

function InitializeUninstall(): Boolean;
begin
  Result := WaitForObsClosed(UninstallSilent);
end;

{ Only the old plugin's own folder, recognised by its DLL — never a guess. }
function LegacyInstalled(): Boolean;
begin
  Result := FileExists(ExpandConstant(LegacyDir + '\bin\64bit\bilibili-stream-for-obs.dll'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('removelegacy') and LegacyInstalled() then
    DelTree(ExpandConstant(LegacyDir), True, True, True);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  { Default keeps user data. Complete cleanup only on an explicit Yes, and only Kaibo's folder. }
  DataDir := ExpandConstant('{userappdata}\obs-studio\plugin_config\obs-kaibo');
  if UninstallSilent or not DirExists(DataDir) then
    Exit;
  if MsgBox(FmtMessage(CustomMessage('RemoveData'), [DataDir]), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
  begin
    if not DelTree(DataDir, True, True, True) then
      MsgBox(FmtMessage(CustomMessage('RemoveDataFailed'), [DataDir]), mbError, MB_OK);
  end;
end;
