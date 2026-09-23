[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$PackageZip,
  [Parameter(Mandatory = $true)][string]$ExpectedSha256,
  [string]$OutputDirectory,
  [string]$CompilerPath,
  [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$zip = [System.IO.Path]::GetFullPath($PackageZip)
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Split-Path -Parent $zip
}
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$name = 'Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only-setup.exe'
$installer = Join-Path $output $name

if ($ExpectedSha256 -notmatch '^[0-9a-fA-F]{64}$') {
  throw 'ExpectedSha256 must be exactly 64 hexadecimal characters.'
}
if (-not (Test-Path -LiteralPath $zip -PathType Leaf)) {
  throw "Alpha ZIP was not found: $zip"
}
$zipItem = Get-Item -LiteralPath $zip -Force
if ($zipItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
  throw 'The alpha ZIP must not be a reparse point.'
}
$actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash
if ($actualSha256 -ne $ExpectedSha256) {
  throw "Alpha ZIP SHA-256 mismatch: $actualSha256"
}
if ((Test-Path -LiteralPath $installer) -and -not $Force) {
  throw "Installer already exists; use -Force to replace this exact output: $installer"
}

if ([string]::IsNullOrWhiteSpace($CompilerPath)) {
  $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if ($null -ne $compiler) {
    $CompilerPath = $compiler.Source
  } else {
    $uninstallKeys = @(
      'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
      'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
      'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1'
    )
    foreach ($key in $uninstallKeys) {
      if (Test-Path -LiteralPath $key) {
        $entry = Get-ItemProperty -LiteralPath $key
        $location = $entry.PSObject.Properties['InstallLocation']
        if ($null -ne $location -and
            -not [string]::IsNullOrWhiteSpace([string]$location.Value)) {
          $candidate = Join-Path ([string]$location.Value) 'ISCC.exe'
          if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $CompilerPath = $candidate
            break
          }
        }
      }
    }
  }
}
if ([string]::IsNullOrWhiteSpace($CompilerPath) -or
    -not (Test-Path -LiteralPath $CompilerPath -PathType Leaf)) {
  throw 'Inno Setup 6.7.1 or newer (ISCC.exe) is required; no compiler was found.'
}

[System.IO.Directory]::CreateDirectory($output) | Out-Null
$iss = Join-Path $PSScriptRoot 'release\Ziliu-Alpha.iss'
& $CompilerPath "/DPackageZip=$zip" "/DPackageSha256=$actualSha256" "/O$output" $iss
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $installer -PathType Leaf)) {
  throw "Inno Setup compilation failed with exit code $LASTEXITCODE."
}
$installerSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $installer).Hash
[System.IO.File]::WriteAllText("$installer.sha256", "$($installerSha256.ToLowerInvariant())  $name`n",
    [System.Text.UTF8Encoding]::new($false))
Write-Host "Created unsigned alpha installer: $installer"
Write-Host "Embedded ZIP SHA-256: $actualSha256"
Write-Host "Installer SHA-256: $installerSha256"
