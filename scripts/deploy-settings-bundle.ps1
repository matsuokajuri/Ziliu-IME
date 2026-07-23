[CmdletBinding()]
param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Release",

  [string]$SourceDirectory,

  [string]$DestinationDirectory
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

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SourceDirectory)) {
  $SourceDirectory =
      Join-Path $repositoryRoot "build\local-x64-$Configuration\bin"
}
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

$runningSettings = Get-Process -Name "ZiliuSettings" -ErrorAction SilentlyContinue
if ($null -ne $runningSettings) {
  throw "Close ZiliuSettings.exe before deploying the settings bundle."
}

New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null

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
