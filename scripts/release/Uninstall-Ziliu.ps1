[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$clsidKey = "Software\Classes\CLSID\{7B9C1D3D-9D4E-4F02-A9A6-3EBA99CDE7B1}\InprocServer32"
$brokerRunKey = "Software\Microsoft\Windows\CurrentVersion\Run"
$brokerRunName = "ZiliuBroker"
$versionRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$manifestPath = Join-Path $versionRoot "install-manifest.sha256"
$receiptPath = Join-Path $versionRoot "cleanup-pending.json"

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
      -not [Environment]::Is64BitProcess) {
    throw "Run Uninstall-Ziliu.ps1 from an elevated 64-bit PowerShell session."
  }
  $nativeArchitecture = if ([string]::IsNullOrWhiteSpace($env:PROCESSOR_ARCHITEW6432)) {
    $env:PROCESSOR_ARCHITECTURE
  } else {
    $env:PROCESSOR_ARCHITEW6432
  }
  if ($nativeArchitecture -ne "AMD64") {
    throw "This alpha candidate supports native x64 Windows only; ARM64 is not supported."
  }
}

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

function Assert-NoReparseTree {
  param([Parameter(Mandatory = $true)][string]$Root)
  $pending = [Collections.Generic.Stack[string]]::new()
  $pending.Push([System.IO.Path]::GetFullPath($Root))
  while ($pending.Count -ne 0) {
    $directory = $pending.Pop()
    $directoryItem = Get-Item -LiteralPath $directory -Force -ErrorAction Stop
    if ($directoryItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
      throw "Managed tree contains a reparse point: $directory"
    }
    foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
      if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        throw "Managed tree contains a reparse point: $($item.FullName)"
      }
      if ($item -is [System.IO.DirectoryInfo]) {
        $pending.Push($item.FullName)
      }
    }
  }
}

function Resolve-SafeRelativePath {
  param([string]$Root, [string]$Relative)
  if ([string]::IsNullOrWhiteSpace($Relative) -or
      [System.IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(":") -or
      $Relative.Contains("\")) {
    throw "Unsafe manifest path: $Relative"
  }
  foreach ($segment in $Relative.Split('/')) {
    if ([string]::IsNullOrEmpty($segment) -or $segment -eq "." -or $segment -eq ".." -or
        $segment.EndsWith(".") -or $segment.EndsWith(" ") -or
        $segment.IndexOfAny([char[]]'<>"|?*') -ge 0 -or
        $segment.ToCharArray().Where({ [int]$_ -lt 32 }).Count -ne 0 -or
        $segment -match "^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$") {
      throw "Unsafe manifest path segment: $segment"
    }
  }
  $prefix = [System.IO.Path]::GetFullPath($Root).TrimEnd("\") + "\"
  $candidate = [System.IO.Path]::GetFullPath((Join-Path $Root $Relative.Replace("/", "\")))
  if (-not $candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Manifest path escapes the managed root: $Relative"
  }
  return $candidate
}

function Get-RegisteredTipPath {
  $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($clsidKey)
  if ($null -eq $key) { return $null }
  try {
    $value = $key.GetValue("")
    if ($value -is [string] -and -not [string]::IsNullOrWhiteSpace($value)) {
      return [System.IO.Path]::GetFullPath($value)
    }
    return $null
  } finally {
    $key.Dispose()
  }
}

function Remove-OwnBrokerRunValue {
  param([Parameter(Mandatory = $true)][string]$Expected)
  $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($brokerRunKey, $true)
  if ($null -eq $key) { return }
  try {
    $actual = $key.GetValue($brokerRunName, $null, "DoNotExpandEnvironmentNames")
    if ($null -eq $actual) { return }
    if (-not [string]::Equals($actual, $Expected,
                             [System.StringComparison]::OrdinalIgnoreCase)) {
      throw "Refusing to remove a startup entry that points outside this installation."
    }
    $key.DeleteValue($brokerRunName, $false)
  } finally { $key.Dispose() }
}

Assert-Administrator
$expectedRoot = Join-Path ([System.IO.Path]::GetFullPath($env:ProgramFiles)) "Ziliu\0.1.0-alpha.1"
if (-not [string]::Equals($versionRoot, $expectedRoot,
                          [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Refusing to uninstall from an unexpected directory: $versionRoot"
}
Assert-NoReparseTree -Root $versionRoot

$entries = @()
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$manifestHash = Get-Sha256 -Path $manifestPath
$cleanupPending = Test-Path -LiteralPath $receiptPath -PathType Leaf
if ($cleanupPending) {
  $receiptItem = Get-Item -LiteralPath $receiptPath -Force
  if ($receiptItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
    throw "Cleanup receipt must not be a reparse point."
  }
  $receipt = Get-Content -Raw -LiteralPath $receiptPath | ConvertFrom-Json
  if ($receipt.version -ne "0.1.0-alpha.1" -or
      $receipt.versionRoot -ne $versionRoot -or
      $receipt.manifestSha256 -ne $manifestHash) {
    throw "Cleanup receipt does not match this managed installation."
  }
  if ($null -ne (Get-RegisteredTipPath)) {
    throw "Cleanup is pending, but a Ziliu version is registered. Refusing to remove files."
  }
}
foreach ($line in Get-Content -LiteralPath $manifestPath -Encoding UTF8) {
  if ($line -notmatch "^([0-9a-fA-F]{64})  (.+)$") { throw "Malformed install manifest." }
  $expectedHash = $Matches[1]
  $relative = $Matches[2]
  $path = Resolve-SafeRelativePath -Root $versionRoot -Relative $relative
  if (-not $seen.Add($path)) { throw "Duplicate install manifest path: $relative" }
  if (Test-Path -LiteralPath $path -PathType Leaf) {
    $item = Get-Item -LiteralPath $path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -or
        (Get-Sha256 -Path $path) -ne $expectedHash) {
      throw "Managed file type or hash mismatch: $relative"
    }
  } elseif (-not $cleanupPending) {
    throw "Managed file is missing before unregister: $relative"
  }
  $entries += [pscustomobject]@{ Relative = $relative; FullPath = $path }
}
if ($entries.Count -eq 0) { throw "The install manifest is empty." }

if (-not $cleanupPending) {
  $expectedTip = Join-Path $versionRoot "ZiliuTIP.dll"
  $registeredTip = Get-RegisteredTipPath
  if ($null -eq $registeredTip -or
      -not [string]::Equals($registeredTip, $expectedTip,
                           [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to unregister: InprocServer32 does not point to this version."
  }
  $register = Join-Path $versionRoot "ZiliuRegister.exe"
  $process = Start-Process -FilePath $register -ArgumentList uninstall -Wait -PassThru `
      -WindowStyle Hidden
  if ($process.ExitCode -ne 0) {
    throw "ZiliuRegister uninstall failed; no program files were removed."
  }
  if ($null -ne (Get-RegisteredTipPath)) {
    throw "COM registration still exists after uninstall; no program files were removed."
  }
  $receipt = [ordered]@{
    version = "0.1.0-alpha.1"
    versionRoot = $versionRoot
    manifestSha256 = $manifestHash
  }
  [System.IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json),
      [System.Text.UTF8Encoding]::new($false))
}

Remove-OwnBrokerRunValue -Expected ('"' + (Join-Path $versionRoot "ZiliuBroker.exe") + '"')

$orderedEntries = @($entries | Sort-Object {
    if ($_.Relative -eq "Uninstall-Ziliu.ps1") { 1 } else { 0 }
  }, Relative)
try {
  foreach ($entry in $orderedEntries) {
    if (Test-Path -LiteralPath $entry.FullPath -PathType Leaf) {
      Remove-Item -LiteralPath $entry.FullPath -Force -ErrorAction Stop
    }
  }
} catch {
  Write-Warning "Registration is removed, but one or more managed files are still loaded. Cleanup remains pending; reboot and run this uninstaller again from the preserved version directory."
  throw
}
Remove-Item -LiteralPath $manifestPath -Force -ErrorAction Stop
Remove-Item -LiteralPath $receiptPath -Force -ErrorAction Stop
$directories = @(Get-ChildItem -LiteralPath $versionRoot -Directory -Recurse -Force |
    Sort-Object { $_.FullName.Length } -Descending)
foreach ($directory in $directories) {
  if (@(Get-ChildItem -LiteralPath $directory.FullName -Force).Count -eq 0) {
    Remove-Item -LiteralPath $directory.FullName -Force
  }
}
$unknown = @(Get-ChildItem -LiteralPath $versionRoot -Force -ErrorAction SilentlyContinue)
if ($unknown.Count -eq 0) {
  Remove-Item -LiteralPath $versionRoot -Force
  $parent = Split-Path -Parent $versionRoot
  if ((Test-Path -LiteralPath $parent) -and
      @(Get-ChildItem -LiteralPath $parent -Force).Count -eq 0) {
    Remove-Item -LiteralPath $parent -Force
  }
} else {
  Write-Warning "Unknown files remain; the version directory was preserved: $versionRoot"
}
Write-Host "TSF/COM registration was removed and managed program files were deleted."
Write-Host "User settings, themes, and Rime data under LocalAppData were preserved."
Write-Warning "Category cleanup and loaded-host convergence still require the isolated guest uninstall gate."
