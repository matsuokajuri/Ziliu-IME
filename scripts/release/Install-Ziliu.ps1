[CmdletBinding()]
param([switch]$VerifyOnly)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$clsidKey = "Software\Classes\CLSID\{7B9C1D3D-9D4E-4F02-A9A6-3EBA99CDE7B1}\InprocServer32"
$brokerRunKey = "Software\Microsoft\Windows\CurrentVersion\Run"
$brokerRunName = "ZiliuBroker"
$payloadRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "payload"))
$manifestPath = Join-Path $PSScriptRoot "manifest.sha256"

function Assert-Platform {
  if (-not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) {
    throw "This candidate requires 64-bit Windows and 64-bit PowerShell."
  }
  if ([Environment]::OSVersion.Version.Build -lt 22000) {
    throw "This alpha candidate supports Windows 11 (build 22000 or later) only."
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

function Assert-Administrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run Install-Ziliu.ps1 from an elevated 64-bit PowerShell session."
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
  param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Relative
  )
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
  $rootPrefix = [System.IO.Path]::GetFullPath($Root).TrimEnd("\") + "\"
  $candidate = [System.IO.Path]::GetFullPath(
      (Join-Path $Root ($Relative.Replace("/", "\"))))
  if (-not $candidate.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Manifest path escapes the managed root: $Relative"
  }
  return $candidate
}

function Read-VerifiedManifest {
  param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Path
  )
  $entries = @()
  $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
    if ($line -notmatch "^([0-9a-fA-F]{64})  (.+)$") {
      throw "Malformed manifest entry."
    }
    $expectedHash = $Matches[1]
    $relative = $Matches[2]
    $fullPath = Resolve-SafeRelativePath -Root $Root -Relative $relative
    if (-not $seen.Add($fullPath)) {
      throw "Duplicate manifest path: $relative"
    }
    $item = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
    if (-not ($item -is [System.IO.FileInfo]) -or
        ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
      throw "Manifest entry is not a regular file: $relative"
    }
    $actual = Get-Sha256 -Path $fullPath
    if ($actual -ne $expectedHash) {
      throw "Payload hash mismatch: $relative"
    }
    $entries += [pscustomobject]@{ Relative = $relative; FullPath = $fullPath }
  }
  if ($entries.Count -eq 0) {
    throw "The payload manifest is empty."
  }
  $actualFiles = @(Get-ChildItem -LiteralPath $Root -File -Recurse -Force)
  if ($actualFiles.Count -ne $entries.Count) {
    throw "Payload contains files not covered by manifest.sha256."
  }
  return $entries
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

function Get-BrokerRunEntry {
  $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($brokerRunKey)
  if ($null -eq $key) { return $null }
  try {
    $value = $key.GetValue($brokerRunName, $null, "DoNotExpandEnvironmentNames")
    if ($null -eq $value) { return $null }
    return [pscustomobject]@{
      Value = $value
      Kind = $key.GetValueKind($brokerRunName)
    }
  }
  finally { $key.Dispose() }
}

function Restore-BrokerRunEntry {
  param(
    [Parameter(Mandatory = $true)][string]$Expected,
    [AllowNull()][object]$Previous
  )
  $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($brokerRunKey, $true)
  if ($null -eq $key) { throw "Cannot restore the current user's startup key." }
  try {
    $current = $key.GetValue($brokerRunName, $null, "DoNotExpandEnvironmentNames")
    if (-not [string]::Equals($current, $Expected,
                             [System.StringComparison]::OrdinalIgnoreCase)) {
      if ($null -ne $Previous -and
          [string]::Equals($current, $Previous.Value,
                           [System.StringComparison]::OrdinalIgnoreCase)) { return }
      throw "The ZiliuBroker startup entry changed during installation."
    }
    if ($null -eq $Previous) {
      $key.DeleteValue($brokerRunName, $false)
    } else {
      $key.SetValue($brokerRunName, $Previous.Value, $Previous.Kind)
    }
  } finally { $key.Dispose() }
}

function Invoke-Register {
  param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][ValidateSet("install", "uninstall")][string]$Operation
  )
  $process = Start-Process -FilePath $Executable -ArgumentList $Operation -Wait -PassThru `
      -WindowStyle Hidden
  return $process.ExitCode
}

function Remove-KnownInstallFiles {
  param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][array]$Entries
  )
  foreach ($entry in $Entries) {
    $path = Resolve-SafeRelativePath -Root $Root -Relative $entry.Relative
    if (Test-Path -LiteralPath $path -PathType Leaf) {
      Remove-Item -LiteralPath $path -Force
    }
  }
  $installedManifest = Join-Path $Root "install-manifest.sha256"
  if (Test-Path -LiteralPath $installedManifest -PathType Leaf) {
    Remove-Item -LiteralPath $installedManifest -Force
  }
  $directories = @(Get-ChildItem -LiteralPath $Root -Directory -Recurse -Force |
      Sort-Object { $_.FullName.Length } -Descending)
  foreach ($directory in $directories) {
    if (@(Get-ChildItem -LiteralPath $directory.FullName -Force).Count -eq 0) {
      Remove-Item -LiteralPath $directory.FullName -Force
    }
  }
  if (@(Get-ChildItem -LiteralPath $Root -Force).Count -eq 0) {
    Remove-Item -LiteralPath $Root -Force
  }
}

Assert-Platform
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
  throw "manifest.sha256 is missing."
}
$manifestItem = Get-Item -LiteralPath $manifestPath -Force
if ($manifestItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
  throw "manifest.sha256 must not be a reparse point."
}
Assert-NoReparseTree -Root $payloadRoot
$entries = @(Read-VerifiedManifest -Root $payloadRoot -Path $manifestPath)
$releasePath = Join-Path $payloadRoot "release.json"
$release = Get-Content -Raw -LiteralPath $releasePath | ConvertFrom-Json
if ($release.product -ne "Ziliu" -or $release.version -ne "0.1.0-alpha.1" -or
    $release.platform -ne "windows-11-x64" -or $release.signed -ne $false -or
    $release.testOnly -ne $true -or $release.windowsAppSdkDeployment -ne "self-contained" -or
    $release.userDataPolicy -ne "preserve") {
  throw "release.json does not describe the expected unsigned local alpha candidate."
}
if ($VerifyOnly) {
  Write-Host "Package verification passed for unsigned local-test candidate $($release.version)."
  Write-Warning "SHA-256 checks provide integrity only; this package is not signed or trusted."
  return
}
Assert-Administrator

$programFilesRoot = [System.IO.Path]::GetFullPath($env:ProgramFiles)
$installRoot = Join-Path $programFilesRoot "Ziliu"
$versionRoot = Join-Path $installRoot $release.version
$brokerRunValue = '"' + (Join-Path $versionRoot "ZiliuBroker.exe") + '"'
$stagingRoot = Join-Path $installRoot ("." + $release.version + ".install-" +
    [Guid]::NewGuid().ToString("N"))
$installRootCreated = $false
if (Test-Path -LiteralPath $installRoot) {
  Assert-NoReparseTree -Root $installRoot
}
if (Test-Path -LiteralPath $versionRoot) {
  throw "Refusing to overwrite an existing version directory: $versionRoot"
}
$previousTipPath = Get-RegisteredTipPath
$previousBrokerRunEntry = Get-BrokerRunEntry
if ($null -ne $previousBrokerRunEntry) {
  if ($null -eq $previousTipPath -or
      $previousBrokerRunEntry.Kind -ne [Microsoft.Win32.RegistryValueKind]::String -or
      $previousBrokerRunEntry.Value -isnot [string]) {
    throw "Refusing to overwrite an unverified ZiliuBroker startup entry."
  }
  $expectedPreviousBroker = '"' +
      (Join-Path (Split-Path -Parent $previousTipPath) "ZiliuBroker.exe") + '"'
  if (-not [string]::Equals($previousBrokerRunEntry.Value, $expectedPreviousBroker,
                           [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to overwrite a ZiliuBroker startup entry that does not match the registered TIP."
  }
}
$brokerRunInstalled = $false
$preserveVersionRoot = $false
try {
  if (-not (Test-Path -LiteralPath $installRoot)) {
    [System.IO.Directory]::CreateDirectory($installRoot) | Out-Null
    $installRootCreated = $true
  }
  [System.IO.Directory]::CreateDirectory($stagingRoot) | Out-Null
  foreach ($entry in $entries) {
    $source = Resolve-SafeRelativePath -Root $payloadRoot -Relative $entry.Relative
    $destination = Resolve-SafeRelativePath -Root $stagingRoot -Relative $entry.Relative
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
  }
  $stagedEntries = @(Read-VerifiedManifest -Root $stagingRoot -Path $manifestPath)
  if ($stagedEntries.Count -ne $entries.Count) {
    throw "The staged payload does not match the verified package manifest."
  }
  Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $stagingRoot "install-manifest.sha256")
  Assert-NoReparseTree -Root $stagingRoot
  [System.IO.Directory]::Move($stagingRoot, $versionRoot)

  $runBeforeSwitch = Get-BrokerRunEntry
  if (-not [string]::Equals((Get-RegisteredTipPath), $previousTipPath,
                            [System.StringComparison]::OrdinalIgnoreCase) -or
      ($null -eq $runBeforeSwitch) -ne ($null -eq $previousBrokerRunEntry) -or
      ($null -ne $runBeforeSwitch -and
       ($runBeforeSwitch.Kind -ne $previousBrokerRunEntry.Kind -or
        -not [string]::Equals($runBeforeSwitch.Value, $previousBrokerRunEntry.Value,
                             [System.StringComparison]::OrdinalIgnoreCase)))) {
    throw "The previous TIP registration or Broker startup entry changed during installation."
  }
  $runKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($brokerRunKey)
  if ($null -eq $runKey) { throw "Cannot open the current user's startup key." }
  try {
    $runKey.SetValue($brokerRunName, $brokerRunValue,
                     [Microsoft.Win32.RegistryValueKind]::String)
    $brokerRunInstalled = $true
  } finally { $runKey.Dispose() }

  $newRegister = Join-Path $versionRoot "ZiliuRegister.exe"
  $newTipPath = Join-Path $versionRoot "ZiliuTIP.dll"
  $installExit = Invoke-Register -Executable $newRegister -Operation install
  $registeredAfterInstall = Get-RegisteredTipPath
  if ($installExit -ne 0 -or $null -eq $registeredAfterInstall -or
      -not [string]::Equals($registeredAfterInstall, $newTipPath,
                            [System.StringComparison]::OrdinalIgnoreCase)) {
    $restored = $false
    if ($null -ne $previousTipPath) {
      $previousRegister = Join-Path (Split-Path -Parent $previousTipPath) "ZiliuRegister.exe"
      if (Test-Path -LiteralPath $previousRegister -PathType Leaf) {
        $restoreExit = Invoke-Register -Executable $previousRegister -Operation install
        $registeredAfterRestore = Get-RegisteredTipPath
        $restored = $restoreExit -eq 0 -and $null -ne $registeredAfterRestore -and
            [string]::Equals($registeredAfterRestore, $previousTipPath,
                             [System.StringComparison]::OrdinalIgnoreCase)
      }
    } else {
      $rollbackExit = Invoke-Register -Executable $newRegister -Operation uninstall
      $restored = $rollbackExit -eq 0 -and $null -eq (Get-RegisteredTipPath)
    }
    if (-not $restored) {
      $preserveVersionRoot = $true
      throw "FAILED: new registration failed and the previous registration state could not be confirmed. The new version directory was preserved for diagnosis: $versionRoot"
    }
    Restore-BrokerRunEntry -Expected $brokerRunValue -Previous $previousBrokerRunEntry
    $brokerRunInstalled = $false
    Remove-KnownInstallFiles -Root $versionRoot -Entries $entries
    throw "Installation failed; the previous registration state was restored."
  }

  Write-Host "Installed unsigned local-test candidate at: $versionRoot"
  Write-Warning "This package is unsigned and is not approved for public release."
  Write-Warning "The registration now points at this version, but loaded TIP/Broker processes may still run the old version. Sign out or reboot, then reopen applications before treating the upgrade as converged."
  Write-Host "User settings, themes, and Rime data under LocalAppData were not modified."
} catch {
  $newTipRegistered = [string]::Equals(
      (Get-RegisteredTipPath), (Join-Path $versionRoot "ZiliuTIP.dll"),
      [System.StringComparison]::OrdinalIgnoreCase)
  if ($brokerRunInstalled -and -not $newTipRegistered -and -not $preserveVersionRoot) {
    Restore-BrokerRunEntry -Expected $brokerRunValue -Previous $previousBrokerRunEntry
  }
  if ((Test-Path -LiteralPath $versionRoot) -and
      -not $newTipRegistered -and -not $preserveVersionRoot) {
    Assert-NoReparseTree -Root $versionRoot
    Remove-KnownInstallFiles -Root $versionRoot -Entries $entries
  }
  if (Test-Path -LiteralPath $stagingRoot) {
    Assert-NoReparseTree -Root $stagingRoot
    Remove-KnownInstallFiles -Root $stagingRoot -Entries $entries
  }
  if ($installRootCreated -and (Test-Path -LiteralPath $installRoot) -and
      @(Get-ChildItem -LiteralPath $installRoot -Force).Count -eq 0) {
    Remove-Item -LiteralPath $installRoot -Force
  }
  throw
}
