<#
.SYNOPSIS
    Prepares the files for hardware test session 1 (pPlay + PS4 IPTV Playback Test).

.DESCRIPTION
    Collects into build\hardware-test-1\ (and optionally copies to a USB stick root):
      - IV0001-PPLA00001_00-PPLA000013080000.pkg   untouched upstream pPlay
      - IV0001-IPTT00001_00-IPTT000010100000.pkg   PS4 IPTV Playback Test
      - test_streams.txt                            from config\test_streams.txt (your URLs, never printed)
      - test.mp4                                    from -TestVideo (optional)
      - HARDWARE-TEST-1.txt                         the checklist

.PARAMETER TestVideo
    Path of a local video to copy as test.mp4 (H.264 + AAC .mp4, 720p/1080p recommended).

.PARAMETER UsbDrive
    Drive letter of the USB stick (e.g. E:). The files are copied to its root.
#>
param(
    [string] $TestVideo,
    [string] $UsbDrive
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Stage = Join-Path $RepoRoot 'build\hardware-test-1'

$pkgs = @(
    (Join-Path $RepoRoot 'build\pplay-ps4\IV0001-PPLA00001_00-PPLA000013080000.pkg'),
    (Join-Path $RepoRoot 'build\playback-test\IV0001-IPTT00001_00-IPTT000010100000.pkg')
)
foreach ($p in $pkgs) {
    if (-not (Test-Path $p)) { throw "missing $p - build it first (scripts\build-pplay-reference.ps1 / build-playback-test.ps1)" }
}

New-Item -ItemType Directory -Force $Stage | Out-Null
Get-ChildItem $Stage -File | Remove-Item -Force

foreach ($p in $pkgs) { Copy-Item $p $Stage }
Copy-Item (Join-Path $RepoRoot 'docs\hardware-test-1.txt') (Join-Path $Stage 'HARDWARE-TEST-1.txt')

# stream config: validate without printing any value
$cfg = Join-Path $RepoRoot 'config\test_streams.txt'
$cfgOk = $false
if (Test-Path $cfg) {
    $lines = Get-Content $cfg | Where-Object { $_ -notmatch '^\s*#' -and $_.Trim() -ne '' }
    $ts  = @($lines | Where-Object { $_ -match '^\s*TS\s*=\s*\S' })
    $hls = @($lines | Where-Object { $_ -match '^\s*HLS\s*=\s*\S' })
    $placeholder = @($lines | Where-Object { $_ -match '<' })
    if ($ts.Count -eq 1 -and $hls.Count -eq 1 -and $placeholder.Count -eq 0) {
        $scheme = @(($ts + $hls) | ForEach-Object { ($_ -split '=', 2)[1].Trim().Split(':')[0].ToLower() })
        if ($scheme -contains 'https') {
            Write-Warning 'a URL uses https: the pPlay FFmpeg build has no https protocol (expect "Protocol not found")'
        }
        Copy-Item $cfg (Join-Path $Stage 'test_streams.txt')
        $cfgOk = $true
    }
}
if (-not $cfgOk) {
    Write-Warning "config\test_streams.txt is missing or not filled in (needs exactly one TS= and one HLS= line, no <placeholders>). Network tests will report 'URL NOT SET'."
}

if ($TestVideo) {
    if (-not (Test-Path $TestVideo)) { throw "test video not found: $TestVideo" }
    Copy-Item $TestVideo (Join-Path $Stage 'test.mp4')
}

Write-Host "Staged in $Stage :"
Get-ChildItem $Stage -File | ForEach-Object {
    $hash = if ($_.Extension -eq '.pkg') { ' sha256 ' + (Get-FileHash $_.FullName).Hash.Substring(0, 16) } else { '' }
    '  {0,-48} {1,12:N0} bytes{2}' -f $_.Name, $_.Length, $hash
}

if ($UsbDrive) {
    $root = $UsbDrive.TrimEnd('\', ':') + ':\'
    if (-not (Test-Path $root)) { throw "USB drive $root not found" }
    $vol = Get-Volume -DriveLetter $root.Substring(0, 1)
    if ($vol.DriveType -ne 'Removable') { Write-Warning "$root is reported as $($vol.DriveType), not Removable - check the drive letter" }
    if ($vol.FileSystem -notin @('exFAT', 'FAT32')) { throw "$root is $($vol.FileSystem); the PS4 needs exFAT or FAT32" }
    $big = Get-ChildItem $Stage -File | Where-Object { $_.Length -ge 4GB }
    if ($vol.FileSystem -eq 'FAT32' -and $big) { throw 'a file is >= 4 GB and the stick is FAT32' }
    Get-ChildItem $Stage -File | ForEach-Object { Copy-Item $_.FullName $root -Force }
    Write-Host "Copied to $root ($($vol.FileSystem)). Remember to delete test_streams.txt from the stick after the test."
}
