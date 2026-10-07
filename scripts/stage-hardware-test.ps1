<#
.SYNOPSIS
    Prepares the files for hardware test session 2 (network TS/HLS playback with the pPlay stack).

.DESCRIPTION
    Session 1 proved local video+audio with untouched pPlay. Session 2 only needs the playback test PKG,
    which already contains the private stream configuration (config\test_streams.txt -> /app0/test_streams.txt),
    so nothing else has to be put on the USB stick.

    THE PKG CONTAINS YOUR IPTV CREDENTIALS. Do not share it; delete it from the stick after installing.

.PARAMETER UsbDrive
    Drive letter of the USB stick (e.g. E:). The files are copied to its root.
#>
param(
    [string] $UsbDrive
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Stage = Join-Path $RepoRoot 'build\hardware-test-2'

$pkg = Join-Path $RepoRoot 'build\playback-test\IV0001-IPTT00001_00-IPTT000010100000.pkg'
if (-not (Test-Path $pkg)) { throw "missing $pkg - run scripts\build-playback-test.ps1 and scripts\verify-playback-test.ps1" }

New-Item -ItemType Directory -Force $Stage | Out-Null
Get-ChildItem $Stage -File | Remove-Item -Force
Copy-Item $pkg $Stage
Copy-Item (Join-Path $RepoRoot 'docs\hardware-test-2.txt') (Join-Path $Stage 'HARDWARE-TEST-2.txt')

Write-Host "Staged in $Stage :"
Get-ChildItem $Stage -File | ForEach-Object {
    $hash = if ($_.Extension -eq '.pkg') { ' sha256 ' + (Get-FileHash $_.FullName).Hash.Substring(0, 16) } else { '' }
    '  {0,-48} {1,12:N0} bytes{2}' -f $_.Name, $_.Length, $hash
}
Write-Warning 'The PKG contains your IPTV credentials (packaged test_streams.txt). Do not share it.'

if ($UsbDrive) {
    $root = $UsbDrive.TrimEnd('\', ':') + ':\'
    if (-not (Test-Path $root)) { throw "USB drive $root not found" }
    $vol = Get-Volume -DriveLetter $root.Substring(0, 1)
    if ($vol.DriveType -ne 'Removable') { Write-Warning "$root is reported as $($vol.DriveType), not Removable - check the drive letter" }
    if ($vol.FileSystem -notin @('exFAT', 'FAT32')) { throw "$root is $($vol.FileSystem); the PS4 needs exFAT or FAT32" }
    Get-ChildItem $Stage -File | ForEach-Object { Copy-Item $_.FullName $root -Force }
    if (Test-Path (Join-Path $root 'test_streams.txt')) {
        Write-Warning "$root still has test_streams.txt from session 1: delete it (it is no longer needed)"
    }
    Write-Host "Copied to $root ($($vol.FileSystem)). Delete the PKG from the stick after installing it."
}
