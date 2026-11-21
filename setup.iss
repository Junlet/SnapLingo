#define MyAppName "SnapLingo"
#define MyAppHomeName "SnapLingo 主页"
#define MyAppLegacyName "AI Screenshot Translator"
#define MyAppExeName "SnapLingo.exe"
#define MyAppLegacyExeName "AI-Screenshot-Translator-Cpp.exe"

[Setup]
; 程序基本信息
AppName={#MyAppName}
AppVersion=1.1.4
; New installs go to a folder that matches the new product name. Upgrades keep
; their existing folder because UsePreviousAppDir defaults to yes: an old user
; stays in "...\AI Screenshot Translator" with storage\ untouched, while a fresh
; install lands in "...\SnapLingo".
DefaultDirName={commonpf}\{#MyAppName}
DefaultGroupName={#MyAppName}
SetupIconFile={#SourcePath}\assets\icon.ico
UninstallDisplayName={#MyAppName}
OutputDir={#SourcePath}\setup
OutputBaseFilename=SnapLingo_Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; 开发者信息
AppPublisher=Junlet
AppPublisherURL=https://github.com/Junlet/SnapLingo
AppSupportURL=https://github.com/Junlet/SnapLingo/issues
; 版权信息
AppCopyright=Copyright (C) 2026 Junlet
; 如果已安装过，自动关闭正在运行的程序，防止文件占用导致安装失败
CloseApplications=yes
RestartApplications=no
; 允许用户在覆盖安装时无需先卸载
AllowNoIcons=yes
; Explicit even though it is Inno's default: an existing install keeps its folder,
; which is what preserves storage\ in place across the rename.
UsePreviousAppDir=yes
; Do NOT reuse the old "AI Screenshot Translator" Start Menu folder: the new
; shortcut must live in the new SnapLingo folder, and RemoveLegacyShortcuts below
; deletes the old folder wholesale.
UsePreviousGroup=no
; Keep the AppId: it is what makes Inno upgrade the existing install in place
; instead of adding a second entry to Apps & Features.
AppId={{c0b26d24-9393-41f3-815b-a13b15fe8f3e}}
; Stay in 32-bit install mode on purpose: the existing 1.0.5 entry lives in the
; 32-bit uninstall key, and switching modes would hide it from Inno's upgrade
; detection. The payload itself is a 64-bit executable.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkablealone

[InstallDelete]
; Remove app-managed runtime files before copying new ones so renamed/removed
; payload files do not linger across upgrades. Do not touch user data files:
; storage\ and .env are deliberately absent from this list.
Type: filesandordirs; Name: "{app}\assets"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"
; The pre-rename binary. Without this line an in-place upgrade leaves the old
; AI-Screenshot-Translator-Cpp.exe next to SnapLingo.exe forever.
Type: files; Name: "{app}\{#MyAppLegacyExeName}"
Type: files; Name: "{app}\D3Dcompiler_47.dll"
Type: files; Name: "{app}\opengl32sw.dll"
Type: files; Name: "{app}\profile.json"
Type: files; Name: "{app}\Qt6Core.dll"
Type: files; Name: "{app}\Qt6Gui.dll"
Type: files; Name: "{app}\Qt6Network.dll"
Type: files; Name: "{app}\Qt6Svg.dll"
Type: files; Name: "{app}\Qt6Widgets.dll"
Type: files; Name: "{app}\WebView2Loader.dll"
Type: files; Name: "{app}\wkf.log"
Type: files; Name: "{app}\debug.log"

[Files]
; Let Inno use normal version checks for versioned binaries.
Source: "{#SourcePath}\build\Release\{#MyAppExeName}"; DestDir: "{app}"
; Compatibility alias: a pinned/dock shortcut may still point at the pre-rename
; executable. Ship the new binary under that name too, so those shortcuts keep
; working (a plain launch defaults to screenshot).
Source: "{#SourcePath}\build\Release\{#MyAppExeName}"; DestDir: "{app}"; DestName: "{#MyAppLegacyExeName}"
Source: "{#SourcePath}\build\Release\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs; Excludes: "{#MyAppExeName},*.log,.stfolder,.stfolder\*,storage,storage\*"
; Preserve an existing user-maintained .env during upgrades.
; skipifsourcedoesntexist: the repo does not ship a .env, so without this flag
; ISCC aborts with "no files found matching" on a clean checkout.
Source: "{#SourcePath}\.env"; DestDir: "{app}"; Flags: onlyifdoesntexist skipifsourcedoesntexist

[Icons]
; The app icon is a screenshot entry point. The dashboard is opened from the
; tray menu, so there is no separate "--home" shortcut that could be mistaken
; for the screenshot icon.
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--screenshot"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--screenshot"; Tasks: desktopicon

[Run]
; 安装结束后打开主页（首次启动），不自动截图
Filename: "{app}\{#MyAppExeName}"; Parameters: "--dashboard"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\wkf.log"
Type: filesandordirs; Name: "{app}\debug.log"

[Code]
// 检查进程是否正在运行的函数
function IsAppRunning(const FileName: string): Boolean;
var
  FSWbemLocator: Variant;
  FWMIService: Variant;
  FWbemObjectSet: Variant;
begin
  Result := false;
  try
    FSWbemLocator := CreateOleObject('WbemScripting.SWbemLocator');
    FWMIService := FSWbemLocator.ConnectServer('', 'root\CIMV2');
    FWbemObjectSet := FWMIService.ExecQuery(Format('SELECT * FROM Win32_Process WHERE Name = "%s"', [FileName]));
    Result := (FWbemObjectSet.Count > 0);
  except
    // 如果 WMI 出错，保守起见返回 False 或记录日志
  end;
end;

// The product was renamed, but an in-place upgrade keeps the historical install
// folder. Remove the old-named shortcuts (and their Start Menu folder) after the
// new SnapLingo ones have been created; never touch storage\ or .env here.
procedure RemoveLegacyShortcuts;
var
  LegacyGroup: String;
begin
  LegacyGroup := ExpandConstant('{commonprograms}\{#MyAppLegacyName}');
  if DirExists(LegacyGroup) then
  begin
    Log('Removing legacy Start Menu folder: ' + LegacyGroup);
    DelTree(LegacyGroup, True, True, True);
  end;

  LegacyGroup := ExpandConstant('{userprograms}\{#MyAppLegacyName}');
  if DirExists(LegacyGroup) then
  begin
    Log('Removing legacy Start Menu folder: ' + LegacyGroup);
    DelTree(LegacyGroup, True, True, True);
  end;

  if DeleteFile(ExpandConstant('{commondesktop}\{#MyAppLegacyName}.lnk')) then
    Log('Removed legacy common desktop shortcut');
  if DeleteFile(ExpandConstant('{userdesktop}\{#MyAppLegacyName}.lnk')) then
    Log('Removed legacy user desktop shortcut');
  if DeleteFile(ExpandConstant('{autodesktop}\{#MyAppLegacyName}.lnk')) then
    Log('Removed legacy desktop shortcut');

  // Older 1.1.0 test builds created a "--home" shortcut. It must not stay
  // around: the app icon is the screenshot entry point now.
  if DeleteFile(ExpandConstant('{group}\{#MyAppHomeName}.lnk')) then
    Log('Removed obsolete SnapLingo home shortcut');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    RemoveLegacyShortcuts;
end;

// The pre-rename executable is removed by [InstallDelete], but Inno's
// CloseApplications only looks at the files being installed. Stop earlier with a
// clear message instead of failing halfway through a file deletion.
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if IsAppRunning('{#MyAppLegacyExeName}') then
    Result := 'The previous AI Screenshot Translator is still running. Please close it, then run setup again.';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ErrorCode: Integer;
begin
  // usAppFunctions 是准备开始执行卸载步骤
  if CurUninstallStep = usUninstall then
  begin
    if IsAppRunning('{#MyAppExeName}') then
    begin
      // 只有在运行的时候才调用强制杀进程
      Exec('taskkill.exe', '/f /im {#MyAppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ErrorCode);
      Sleep(800); // 稍微多给一点点时间，确保 OS 释放文件句柄
    end;
    // An install upgraded from the old name may still have the legacy process
    // holding files under the shared install folder.
    if IsAppRunning('{#MyAppLegacyExeName}') then
    begin
      Exec('taskkill.exe', '/f /im {#MyAppLegacyExeName}', '', SW_HIDE, ewWaitUntilTerminated, ErrorCode);
      Sleep(800);
    end;
  end;
end;
