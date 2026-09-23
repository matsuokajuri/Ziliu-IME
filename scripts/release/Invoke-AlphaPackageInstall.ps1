[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$PackageZip,
  [Parameter(Mandatory = $true)][string]$ExpectedSha256,
  [Parameter(Mandatory = $true)][string]$WorkDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

try {
  if ($ExpectedSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'The expected package SHA-256 is malformed.'
  }
  $actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $PackageZip).Hash
  if ($actualSha256 -ne $ExpectedSha256) {
    throw "The embedded alpha ZIP failed SHA-256 verification: $actualSha256"
  }
  $extracted = Join-Path $WorkDirectory 'expanded'
  if (Test-Path -LiteralPath $extracted) {
    throw "Refusing to overwrite an existing extraction directory: $extracted"
  }
  Expand-Archive -LiteralPath $PackageZip -DestinationPath $extracted
  $installer = Join-Path $extracted 'Install-Ziliu.ps1'
  if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw 'The embedded alpha ZIP has no Install-Ziliu.ps1.'
  }
  & $installer -VerifyOnly
  & $installer
  [Console]::Out.WriteLine('Ziliu alpha installation completed. Sign out or restart before acceptance testing.')
  exit 0
} catch {
  [Console]::Error.WriteLine($_.Exception.Message)
  exit 1
}
