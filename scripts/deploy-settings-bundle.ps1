[CmdletBinding()]
param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Release",

  [string]$SourceDirectory,

  [string]$DestinationDirectory,

  [switch]$SkipTip
)

$ErrorActionPreference = "Stop"

function Get-Sha256 {
  param([Parameter(Mandatory = $true)][string]$Path)

  $stream = [System.IO.File]::OpenRead($Path)
  try {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
      return [System.BitConverter]::ToString(
          $algorithm.ComputeHash($stream)).Replace("-", "")
    } finally {
      $algorithm.Dispose()
    }
  } finally {
    $stream.Dispose()
  }
}

function Get-TipHostProcesses {
  $tasklistPath = Join-Path $env:SystemRoot "System32\tasklist.exe"
  if (-not (Test-Path -LiteralPath $tasklistPath -PathType Leaf)) {
    return @()
  }

  $rows = & $tasklistPath /M ZiliuTIP.dll /FO CSV /NH 2>$null
  $hosts = @()
  foreach ($row in $rows) {
    if (-not $row.StartsWith('"')) {
      continue
    }
    $fields = $row | ConvertFrom-Csv -Header ImageName, ProcessId, SessionName,
        SessionNumber, MemoryUsage
    $hosts += "$($fields.ImageName) (PID $($fields.ProcessId))"
  }
  return $hosts
}

function Get-RegisteredTipPath {
  $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
      "Software\Classes\CLSID\{7B9C1D3D-9D4E-4F02-A9A6-3EBA99CDE7B1}\InprocServer32")
  if ($null -eq $key) {
    return $null
  }
  try {
    $value = $key.GetValue("")
    if ($value -is [string] -and
        -not [string]::IsNullOrWhiteSpace($value)) {
      return [System.IO.Path]::GetFullPath($value)
    }
    return $null
  } finally {
    $key.Dispose()
  }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SourceDirectory)) {
  $SourceDirectory =
      Join-Path $repositoryRoot "build\local-x64-$Configuration\bin"
}
$destinationWasExplicit =
    -not [string]::IsNullOrWhiteSpace($DestinationDirectory)
if ([string]::IsNullOrWhiteSpace($DestinationDirectory)) {
  $DestinationDirectory =
      Join-Path $repositoryRoot "build\validated-x64-$Configuration\bin"
}

$sourcePath = [System.IO.Path]::GetFullPath($SourceDirectory)
$destinationPath = [System.IO.Path]::GetFullPath($DestinationDirectory)
if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
  throw "Settings build output does not exist: $sourcePath"
}

$settingsBundle = @(
  "App.xbf",
  "MainWindow.xbf",
  "Microsoft.Web.WebView2.Core.dll",
  "Microsoft.Web.WebView2.Core.winmd",
  "Microsoft.WindowsAppRuntime.Bootstrap.dll",
  "ZiliuSettings.pri",
  "ZiliuSettings.winmd",
  "ZiliuSettings.exe"
)

foreach ($fileName in $settingsBundle) {
  $sourceFile = Join-Path $sourcePath $fileName
  if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
    throw "Settings bundle is incomplete; missing: $sourceFile"
  }
}

$tipSourceFile = Join-Path $sourcePath "ZiliuTIP.dll"
if (-not $SkipTip -and
    -not (Test-Path -LiteralPath $tipSourceFile -PathType Leaf)) {
  throw "TIP build output is missing: $tipSourceFile"
}

$runningSettings = Get-Process -Name "ZiliuSettings" -ErrorAction SilentlyContinue
if ($null -ne $runningSettings) {
  throw "Close ZiliuSettings.exe before deploying the settings bundle."
}

New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null

if ($SkipTip) {
  Write-Host "Skipped ZiliuTIP.dll deployment by request."
} else {
  $tipDestinationFile = Join-Path $destinationPath "ZiliuTIP.dll"
  $tipSourceHash = Get-Sha256 -Path $tipSourceFile
  $tipDestinationHash = ""
  if (Test-Path -LiteralPath $tipDestinationFile -PathType Leaf) {
    try {
      $tipDestinationHash =
        Get-Sha256 -Path $tipDestinationFile
    } catch {
      # A loaded in-process COM DLL can deny read sharing. Treat it as stale so
      # the verified pending copy and actionable lock diagnostic are produced.
      $tipDestinationHash = ""
    }
  }

  $registeredTipPath = Get-RegisteredTipPath
  $registeredTipTargetsDestination =
      $null -ne $registeredTipPath -and
      [string]::Equals($registeredTipPath, $tipDestinationFile,
                       [System.StringComparison]::OrdinalIgnoreCase)
  if (-not $destinationWasExplicit -and
      $null -ne $registeredTipPath -and
      -not $registeredTipTargetsDestination) {
    throw ("The registered TIP points to '$registeredTipPath', not " +
           "'$tipDestinationFile'. Specify the registered destination " +
           "explicitly or perform the separate manual registration step.")
  }

  if ($tipSourceHash -ne $tipDestinationHash) {
    # Keep a fully verified side-by-side copy when the registered DLL is loaded
    # by an application. No settings files are changed until the TIP preflight
    # succeeds, avoiding a new-settings/old-runtime split.
    $tipPendingFile = Join-Path $destinationPath "ZiliuTIP.pending.dll"
    Copy-Item -LiteralPath $tipSourceFile -Destination $tipPendingFile -Force
    if ((Get-Sha256 -Path $tipPendingFile) -ne $tipSourceHash) {
      throw "Staged TIP failed SHA-256 verification: $tipPendingFile"
    }

    $hosts =
        if ($registeredTipTargetsDestination) {
          @(Get-TipHostProcesses)
        } else {
          @()
        }
    if ($hosts.Count -ne 0) {
      $hostMessage = $hosts -join ", "
      throw ("The new TIP has been staged at '$tipPendingFile', but the " +
             "registered DLL is still loaded by $hostMessage. Close or " +
             "restart those hosts, then run this deployment command again. " +
             "No TSF registration change is required.")
    }

    try {
      Copy-Item -LiteralPath $tipPendingFile -Destination $tipDestinationFile `
          -Force -ErrorAction Stop
    } catch {
      throw ("The new TIP has been staged at '$tipPendingFile', but " +
             "'$tipDestinationFile' is locked. Close the application using " +
             "it, then run this deployment command again.")
    }

    $tipDestinationHash = Get-Sha256 -Path $tipDestinationFile
    if ($tipDestinationHash -ne $tipSourceHash) {
      throw "Deployed TIP failed SHA-256 verification: $tipDestinationFile"
    }
    Remove-Item -LiteralPath $tipPendingFile -Force
    Write-Host "Deployed and verified ZiliuTIP.dll:"
    Write-Host "  $tipSourceFile"
    Write-Host "  -> $tipDestinationFile"
  } else {
    Write-Host "ZiliuTIP.dll is already current:"
    Write-Host "  $tipDestinationFile"
  }

  if ($registeredTipTargetsDestination -and
      (Get-Sha256 -Path $registeredTipPath) -ne $tipSourceHash) {
    throw "The registered TIP does not match the current build: $registeredTipPath"
  }
}

# XAML binaries and PRI resources are generated together with the executable.
# Deploy the executable last so a partially copied bundle cannot start with a
# new executable and stale resources.
foreach ($fileName in $settingsBundle) {
  $sourceFile = Join-Path $sourcePath $fileName
  $destinationFile = Join-Path $destinationPath $fileName
  Copy-Item -LiteralPath $sourceFile -Destination $destinationFile -Force

  $sourceHash = Get-Sha256 -Path $sourceFile
  $destinationHash = Get-Sha256 -Path $destinationFile
  if ($sourceHash -ne $destinationHash) {
    throw "Deployed file failed SHA-256 verification: $destinationFile"
  }
}

Write-Host "Deployed and verified the WinUI settings bundle:"
Write-Host "  $sourcePath"
Write-Host "  -> $destinationPath"
