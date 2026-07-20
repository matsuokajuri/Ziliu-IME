$ErrorActionPreference = 'Stop'

$assetName = 'rime-33e7814-Windows-msvc-x64.7z'
$assetUrl = "https://github.com/rime/librime/releases/download/1.17.0/$assetName"
$expectedSha256 = '7478c7caa4ff6b37de86daba1f7ce4a994a4f5ba24872a820fb2b3a9b01fed15'

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cacheRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot '.cache'))
if (-not $cacheRoot.StartsWith($repositoryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Resolved cache directory escaped the repository root.'
}

$downloadDirectory = Join-Path $cacheRoot 'downloads'
$archivePath = Join-Path $downloadDirectory $assetName
$runtimeDirectory = Join-Path $cacheRoot 'librime-runtime'
$runtimeDll = Join-Path $runtimeDirectory 'dist\lib\rime.dll'
New-Item -ItemType Directory -Force -Path $downloadDirectory | Out-Null

$downloadRequired = $true
if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
    $downloadRequired = $actualHash -ne $expectedSha256
}

if ($downloadRequired) {
    Invoke-WebRequest -Uri $assetUrl -OutFile $archivePath
}

$actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
if ($actualHash -ne $expectedSha256) {
    throw "librime archive checksum mismatch: expected $expectedSha256, got $actualHash"
}

$sevenZip = Get-Command 7z.exe -ErrorAction Stop
New-Item -ItemType Directory -Force -Path $runtimeDirectory | Out-Null
& $sevenZip.Source x -y "-o$runtimeDirectory" $archivePath | Out-Host
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $runtimeDll -PathType Leaf)) {
    throw 'Failed to extract the verified librime runtime.'
}

Write-Host "Verified librime runtime: $runtimeDll"
