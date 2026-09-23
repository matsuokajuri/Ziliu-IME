#ifndef PackageZip
  #error PackageZip must be supplied by package-alpha-installer.ps1
#endif
#ifndef PackageSha256
  #error PackageSha256 must be supplied by package-alpha-installer.ps1
#endif

#define AppVersion "0.1.0-alpha.1"
#define PackageName "Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only.zip"

[Setup]
AppId=Ziliu.0.1.0-alpha.1
AppName=Ziliu (unsigned alpha prerelease)
AppVersion={#AppVersion}
AppPublisher=Ziliu Project
AppComments=Unsigned early test release; Windows SmartScreen may warn or block installation.
DefaultDirName={autopf}\Ziliu
DisableDirPage=yes
UsePreviousAppDir=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.22000
CloseApplications=no
RestartApplications=no
OutputBaseFilename=Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppVersion}\ZiliuSettings.exe
UninstallLogging=yes
InfoBeforeFile=Alpha-Installer-Notice.txt

[Files]
Source: "{#PackageZip}"; DestName: "{#PackageName}"; Flags: dontcopy
Source: "Invoke-AlphaPackageInstall.ps1"; Flags: dontcopy

[Code]
function OutputSummary(const Output: TExecOutput): String;
var
  I: Integer;
begin
  Result := '';
  for I := 0 to GetArrayLength(Output.StdOut) - 1 do begin
    Log(Output.StdOut[I]);
    if Trim(Output.StdOut[I]) <> '' then Result := Output.StdOut[I];
  end;
  for I := 0 to GetArrayLength(Output.StdErr) - 1 do begin
    Log(Output.StdErr[I]);
    if Trim(Output.StdErr[I]) <> '' then Result := Output.StdErr[I];
  end;
  if Length(Result) > 500 then Result := Copy(Result, 1, 500);
end;

function RunPowerShell(const Parameters: String; var ExitCode: Integer; var Detail: String): Boolean;
var
  Output: TExecOutput;
begin
  ExitCode := -1;
  Detail := '';
  try
    Result := ExecAndCaptureOutput(
      ExpandConstant('{sysnative}\WindowsPowerShell\v1.0\powershell.exe'),
      Parameters, '', SW_SHOWNORMAL, ewWaitUntilTerminated, ExitCode, Output);
    Detail := OutputSummary(Output);
    if Output.Error then Detail := 'PowerShell output capture was incomplete.';
  except
    Result := False;
    Detail := GetExceptionMessage;
    Log(Detail);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
  Detail: String;
  Parameters: String;
  Bootstrap: String;
  PackagePath: String;
begin
  Result := '';
  try
    ExtractTemporaryFile('{#PackageName}');
    ExtractTemporaryFile('Invoke-AlphaPackageInstall.ps1');
  except
    Result := 'Could not extract the embedded alpha package: ' + GetExceptionMessage;
    exit;
  end;
  Bootstrap := ExpandConstant('{tmp}\Invoke-AlphaPackageInstall.ps1');
  PackagePath := ExpandConstant('{tmp}\{#PackageName}');
  Parameters := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
    Bootstrap + '" -PackageZip "' + PackagePath +
    '" -ExpectedSha256 "{#PackageSha256}" -WorkDirectory "' +
    ExpandConstant('{tmp}') + '"';
  if not RunPowerShell(Parameters, ExitCode, Detail) or (ExitCode <> 0) then begin
    Result := 'Ziliu alpha installation failed (exit code ' + IntToStr(ExitCode) + ').';
    if Detail <> '' then Result := Result + #13#10 + Detail;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ExitCode: Integer;
  Detail: String;
  Uninstaller: String;
  Parameters: String;
begin
  if CurUninstallStep <> usUninstall then exit;
  Uninstaller := ExpandConstant('{app}\{#AppVersion}\Uninstall-Ziliu.ps1');
  Parameters := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
    Uninstaller + '"';
  if not RunPowerShell(Parameters, ExitCode, Detail) or (ExitCode <> 0) then begin
    if Detail = '' then Detail := 'The managed uninstaller did not complete.';
    MsgBox('Ziliu could not be safely uninstalled. The uninstall entry is retained.' + #13#10 +
      Detail, mbError, MB_OK);
    Abort;
  end;
end;
