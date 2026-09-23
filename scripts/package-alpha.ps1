[CmdletBinding()]
param(
  [string]$SourceDirectory,
  [string]$DependencyRoot = $env:ZILIU_DEPENDENCY_ROOT,
  [string]$OutputDirectory,
  [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$version = "0.1.0-alpha.1"
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($SourceDirectory)) {
  $SourceDirectory = Join-Path $repositoryRoot "build\local-x64-Release\bin"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repositoryRoot "build\release"
}
if ([string]::IsNullOrWhiteSpace($DependencyRoot)) {
  throw "Set ZILIU_DEPENDENCY_ROOT to the prepared checkout used for this build."
}

$sourcePath = [System.IO.Path]::GetFullPath($SourceDirectory)
$dependencyPath = [System.IO.Path]::GetFullPath($DependencyRoot)
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
$artifactName = "Ziliu-$version-win11-x64-unsigned-test-only.zip"
$artifactPath = Join-Path $outputPath $artifactName
$artifactHashPath = "$artifactPath.sha256"

function Assert-RegularFile {
  param([Parameter(Mandatory = $true)][string]$Path)
  $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
  if (-not ($item -is [System.IO.FileInfo]) -or
      ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
    throw "Expected a regular non-reparse file: $Path"
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

function Get-RegularFilesNoReparse {
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
      } elseif ($item -is [System.IO.FileInfo]) {
        Write-Output $item
      }
    }
  }
}

function Copy-ReleaseFile {
  param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$RelativeDestination
  )
  Assert-RegularFile -Path $Source
  $destination = Join-Path $payloadPath $RelativeDestination
  $parent = Split-Path -Parent $destination
  [System.IO.Directory]::CreateDirectory($parent) | Out-Null
  Copy-Item -LiteralPath $Source -Destination $destination
}

function Get-RelativeChildPath {
  param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Child
  )
  $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd("\") + "\"
  $childFull = [System.IO.Path]::GetFullPath($Child)
  if (-not $childFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Path is not below the expected root: $childFull"
  }
  return $childFull.Substring($rootFull.Length)
}

if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
  throw "Release build output is missing: $sourcePath"
}
if (-not (Test-Path -LiteralPath $dependencyPath -PathType Container)) {
  throw "Dependency root is missing: $dependencyPath"
}
if ((Test-Path -LiteralPath $artifactPath) -or (Test-Path -LiteralPath $artifactHashPath)) {
  if (-not $Force) {
    throw "Candidate already exists; pass -Force to replace this exact artifact: $artifactPath"
  }
  Remove-Item -LiteralPath $artifactPath, $artifactHashPath -Force -ErrorAction SilentlyContinue
}
[System.IO.Directory]::CreateDirectory($outputPath) | Out-Null

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "ZiliuRelease-" + [Guid]::NewGuid().ToString("N"))
[System.IO.Directory]::CreateDirectory($temporaryRoot) | Out-Null
try {
  $packageRoot = Join-Path $temporaryRoot "package"
  $payloadPath = Join-Path $packageRoot "payload"
  [System.IO.Directory]::CreateDirectory($payloadPath) | Out-Null

  $coreFiles = @("ZiliuBroker.exe", "ZiliuRegister.exe", "ZiliuTIP.dll", "rime.dll")
  foreach ($fileName in $coreFiles) {
    Copy-ReleaseFile -Source (Join-Path $sourcePath $fileName) `
        -RelativeDestination $fileName
  }

  $runtimeListPath = Join-Path $PSScriptRoot "release\winui-self-contained-x64.txt"
  Assert-RegularFile -Path $runtimeListPath
  $runtimeFiles = @(Get-Content -LiteralPath $runtimeListPath | Where-Object {
      -not [string]::IsNullOrWhiteSpace($_)
    })
  if ($runtimeFiles.Count -eq 0 -or $runtimeFiles.Count -ne ($runtimeFiles | Sort-Object -Unique).Count) {
    throw "The WinUI self-contained allowlist is empty or contains duplicates."
  }
  foreach ($fileName in $runtimeFiles) {
    if ([System.IO.Path]::IsPathRooted($fileName) -or $fileName.Contains("\") -or
        $fileName.Contains("/") -or $fileName.Contains("..")) {
      throw "Unsafe WinUI allowlist entry: $fileName"
    }
    Copy-ReleaseFile -Source (Join-Path $sourcePath $fileName) `
        -RelativeDestination $fileName
  }

  # WindowsAppSDKSelfContained emits runtime resources below the executable
  # directory as well as root-level binaries. Keep this list constrained to the
  # exact XAML assets and satellite filenames produced by MSBuild; omitting these
  # directories yields an incomplete unpackaged WinUI deployment.
  foreach ($relativeDirectory in @("Microsoft.UI.Xaml", "Microsoft.UI.Xaml\Assets")) {
    $directoryItem = Get-Item -LiteralPath (Join-Path $sourcePath $relativeDirectory) `
        -Force -ErrorAction Stop
    if (-not ($directoryItem -is [System.IO.DirectoryInfo]) -or
        ($directoryItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
      throw "WinUI asset directory must be a non-reparse directory: $($directoryItem.FullName)"
    }
  }
  foreach ($relative in @(
      "Microsoft.UI.Xaml\Assets\NoiseAsset_256X256_PNG.png",
      "Microsoft.UI.Xaml\Assets\map.html")) {
    Copy-ReleaseFile -Source (Join-Path $sourcePath $relative) `
        -RelativeDestination $relative
  }
  $localeDirectories = @(Get-ChildItem -LiteralPath $sourcePath -Directory -Force | Where-Object {
      $_.Name -match "^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8}){1,2}$"
    } | Sort-Object Name)
  if ($localeDirectories.Count -eq 0) {
    throw "The WinUI self-contained output has no locale resource directories."
  }
  foreach ($localeDirectory in $localeDirectories) {
    if ($localeDirectory.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
      throw "WinUI locale directory must not be a reparse point: $($localeDirectory.FullName)"
    }
    $localeFiles = @(Get-RegularFilesNoReparse -Root $localeDirectory.FullName)
    $expectedLocaleFiles = @("Microsoft.UI.Xaml.Phone.dll.mui", "Microsoft.ui.xaml.dll.mui")
    if ($localeFiles.Count -ne $expectedLocaleFiles.Count -or
        @($localeFiles | Where-Object { $_.Name -notin $expectedLocaleFiles }).Count -ne 0) {
      throw "Unexpected WinUI locale payload in: $($localeDirectory.FullName)"
    }
    foreach ($fileName in $expectedLocaleFiles) {
      Copy-ReleaseFile -Source (Join-Path $localeDirectory.FullName $fileName) `
          -RelativeDestination (Join-Path $localeDirectory.Name $fileName)
    }
  }

  $expectedRime = Join-Path $dependencyPath ".cache\librime-runtime\dist\lib\rime.dll"
  Assert-RegularFile -Path $expectedRime
  $builtRime = Join-Path $sourcePath "rime.dll"
  if ((Get-Sha256 -Path $builtRime) -ne (Get-Sha256 -Path $expectedRime)) {
    throw "The staged rime.dll does not match the verified dependency runtime."
  }

  $rimeSource = Join-Path $sourcePath "data\rime"
  if (-not (Test-Path -LiteralPath $rimeSource -PathType Container)) {
    throw "Staged Rime data is missing: $rimeSource"
  }
  $rimeRootItem = Get-Item -LiteralPath $rimeSource -Force
  if ($rimeRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
    throw "Rime data root must not be a reparse point: $rimeSource"
  }
  $allowedRimeExtensions = @(".json", ".lua", ".opencc", ".png", ".tsv", ".txt", ".webp", ".yaml", ".yml")
  $rimeFiles = @(Get-RegularFilesNoReparse -Root $rimeSource | Where-Object {
      $relative = Get-RelativeChildPath -Root $rimeSource -Child $_.FullName
      $parts = $relative -split "[\\/]"
      $parts[0] -notin @(".git", ".github", "build") -and
          $_.Extension.ToLowerInvariant() -in $allowedRimeExtensions
    })
  foreach ($required in @("default.yaml", "default.custom.yaml", "rime_ice.schema.yaml",
                           "rime_ice.dict.yaml", "rime_ice.custom.yaml",
                           "ziliu_private.schema.yaml")) {
    if (-not ($rimeFiles.Name -contains $required)) {
      throw "Required Rime data is missing: $required"
    }
  }
  foreach ($file in $rimeFiles) {
    if ($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
      throw "Rime data contains a reparse file: $($file.FullName)"
    }
    $relative = Get-RelativeChildPath -Root $rimeSource -Child $file.FullName
    Copy-ReleaseFile -Source $file.FullName `
        -RelativeDestination (Join-Path "data\rime" $relative)
  }

  foreach ($binaryName in @("ZiliuBroker.exe", "ZiliuRegister.exe", "ZiliuSettings.exe", "ZiliuTIP.dll")) {
    $binary = Get-Item -LiteralPath (Join-Path $payloadPath $binaryName)
    if ($binary.VersionInfo.FileVersion -ne $version -or
        $binary.VersionInfo.ProductVersion -ne $version) {
      throw "$binaryName has inconsistent version resources: FileVersion='$($binary.VersionInfo.FileVersion)', ProductVersion='$($binary.VersionInfo.ProductVersion)'."
    }
  }

  Copy-ReleaseFile -Source (Join-Path $repositoryRoot "LICENSE") `
      -RelativeDestination "licenses\Ziliu-LICENSE"
  Copy-ReleaseFile -Source (Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md") `
      -RelativeDestination "licenses\THIRD_PARTY_NOTICES.md"
  Copy-ReleaseFile -Source (Join-Path $dependencyPath "third_party\librime\LICENSE") `
      -RelativeDestination "licenses\librime-LICENSE"
  Copy-ReleaseFile -Source (Join-Path $dependencyPath "third_party\rime-ice\LICENSE") `
      -RelativeDestination "licenses\rime-ice-LICENSE"
  Copy-ReleaseFile -Source (Join-Path $dependencyPath "third_party\rime-ice\others\docs\Credits.md") `
      -RelativeDestination "licenses\rime-ice-Credits.md"
  $windowsAppSdkPackage = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.windowsappsdk\2.2.0"
  Copy-ReleaseFile -Source (Join-Path $windowsAppSdkPackage "license.txt") `
      -RelativeDestination "licenses\WindowsAppSDK-LICENSE.txt"
  Copy-ReleaseFile -Source (Join-Path $windowsAppSdkPackage "NOTICE.txt") `
      -RelativeDestination "licenses\WindowsAppSDK-NOTICE.txt"
  Copy-ReleaseFile -Source (Join-Path $PSScriptRoot "release\UNSIGNED-TEST-ONLY.txt") `
      -RelativeDestination "UNSIGNED-TEST-ONLY.txt"
  Copy-ReleaseFile -Source (Join-Path $PSScriptRoot "release\Uninstall-Ziliu.ps1") `
      -RelativeDestination "Uninstall-Ziliu.ps1"

  $git = Get-Command git.exe -ErrorAction Stop
  $sourceCommit = (& $git.Source -C $repositoryRoot rev-parse HEAD).Trim()
  if ($LASTEXITCODE -ne 0 -or $sourceCommit -notmatch "^[0-9a-f]{40}$") {
    throw "Unable to resolve the source commit."
  }
  $trackedStatus = @(& $git.Source -C $repositoryRoot status --porcelain --untracked-files=no)
  if ($LASTEXITCODE -ne 0) {
    throw "Unable to resolve tracked source status."
  }
  $release = [ordered]@{
    product = "Ziliu"
    version = $version
    channel = "alpha"
    platform = "windows-11-x64"
    signed = $false
    testOnly = $true
    sourceCommit = $sourceCommit
    sourceTrackedClean = ($trackedStatus.Count -eq 0)
    windowsAppSdkDeployment = "self-contained"
    userDataPolicy = "preserve"
  }
  $release | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $payloadPath "release.json") `
      -Encoding UTF8

  $payloadFiles = @(Get-ChildItem -LiteralPath $payloadPath -File -Recurse | Sort-Object FullName)
  $manifestLines = foreach ($file in $payloadFiles) {
    $relative = (Get-RelativeChildPath -Root $payloadPath -Child $file.FullName).Replace("\", "/")
    $hash = (Get-Sha256 -Path $file.FullName).ToLowerInvariant()
    "$hash  $relative"
  }
  $manifestPath = Join-Path $packageRoot "manifest.sha256"
  [System.IO.File]::WriteAllLines($manifestPath, $manifestLines,
      [System.Text.UTF8Encoding]::new($false))
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot "release\Install-Ziliu.ps1") `
      -Destination (Join-Path $packageRoot "Install-Ziliu.ps1")

  Add-Type -AssemblyName System.IO.Compression
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $archive = [System.IO.Compression.ZipFile]::Open(
      $artifactPath, [System.IO.Compression.ZipArchiveMode]::Create)
  try {
    $packageFiles = @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse | Sort-Object FullName)
    foreach ($file in $packageFiles) {
      $entryName = (Get-RelativeChildPath -Root $packageRoot -Child $file.FullName).Replace("\", "/")
      $entry = $archive.CreateEntry($entryName, [System.IO.Compression.CompressionLevel]::Optimal)
      $entry.LastWriteTime = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
      $input = [System.IO.File]::OpenRead($file.FullName)
      $output = $entry.Open()
      try {
        $input.CopyTo($output)
      } finally {
        $output.Dispose()
        $input.Dispose()
      }
    }
  } finally {
    $archive.Dispose()
  }

  $artifactHash = (Get-Sha256 -Path $artifactPath).ToLowerInvariant()
  [System.IO.File]::WriteAllText($artifactHashPath, "$artifactHash  $artifactName`n",
      [System.Text.UTF8Encoding]::new($false))
  Write-Host "Created unsigned alpha test package:"
  Write-Host "  $artifactPath"
  Write-Host "  SHA-256: $artifactHash"
} finally {
  if (Test-Path -LiteralPath $temporaryRoot) {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
  }
}
