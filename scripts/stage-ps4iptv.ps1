<#
.SYNOPSIS
    Stages the production PS4 IPTV PKG + checkpoint checklist (and optionally copies them to a USB stick).
    Runs scripts\verify-ps4iptv.ps1 first and refuses to stage a build that fails any release check.
#>
param(
    [string] $Checkpoint = 'checkpoint-1',
    [string] $UsbDrive,
    [switch] $SkipVerify
)
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Stage = Join-Path $RepoRoot "build\$Checkpoint"

if (-not $SkipVerify) {
    & (Join-Path $PSScriptRoot 'verify-ps4iptv.ps1') | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'release verification failed: not staging this build' }
}
$pkg = Get-ChildItem (Join-Path $RepoRoot 'build\ps4iptv-release') -Filter 'IV0001-IPTV00002_*.pkg' | Select-Object -First 1
if (-not $pkg) { throw 'production PKG not found: run scripts\build-ps4iptv.ps1' }

New-Item -ItemType Directory -Force $Stage | Out-Null
Get-ChildItem $Stage -File | Remove-Item -Force
Copy-Item $pkg.FullName $Stage
$doc = Join-Path $RepoRoot "docs\$Checkpoint.txt"
if (Test-Path $doc) { Copy-Item $doc (Join-Path $Stage ($Checkpoint.ToUpper() + '.txt')) }

Write-Host "Staged in $Stage :"
Get-ChildItem $Stage -File | ForEach-Object {
    $hash = if ($_.Extension -eq '.pkg') { ' sha256 ' + (Get-FileHash $_.FullName).Hash.Substring(0, 16) } else { '' }
    '  {0,-48} {1,12:N0} bytes{2}' -f $_.Name, $_.Length, $hash
}

if ($UsbDrive) {
    $root = $UsbDrive.TrimEnd('\', ':') + ':\'
    if (-not (Test-Path $root)) { throw "USB drive $root not found" }
    $vol = Get-Volume -DriveLetter $root.Substring(0, 1)
    if ($vol.FileSystem -notin @('exFAT', 'FAT32')) { throw "$root is $($vol.FileSystem); the PS4 needs exFAT or FAT32" }
    Get-ChildItem $Stage -File | ForEach-Object { Copy-Item $_.FullName $root -Force }
    Write-Host "Copied to $root ($($vol.FileSystem))."
}
