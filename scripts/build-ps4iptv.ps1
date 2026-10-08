<#
.SYNOPSIS
    Builds the PS4 IPTV production PKG (title IPTV00002) natively on Windows.

.DESCRIPTION
    configure -> compile -> link -> fself (eboot.bin) -> credential guard -> SFO -> GP4 -> PKG.
    Requires scripts\setup-toolchain.ps1 and the pPlay checkout (scripts\build-pplay-reference.ps1).
    Output: build\ps4iptv-<type>\IV0001-IPTV00002_00-IPTV000020001000.pkg
#>
param(
    [ValidateSet('Release', 'Debug')] [string] $BuildType = 'Release',
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Toolchain = Join-Path $RepoRoot 'toolchain'
$Build = Join-Path $RepoRoot ("build\ps4iptv-" + $BuildType.ToLower())

if (-not (Test-Path "$RepoRoot\external\pplay-reference\libcross2d\CMakeLists.txt")) {
    throw 'pPlay checkout missing: run scripts\build-pplay-reference.ps1 first'
}

# Windows PowerShell 5.1 turns native stderr output into terminating errors under 'Stop'
$ErrorActionPreference = 'Continue'
$env:OPENORBIS = "$Toolchain\pacbrew\opt\pacbrew\ps4\openorbis" -replace '\\', '/'
$env:OO_PS4_TOOLCHAIN = $env:OPENORBIS
$cmake = "$Toolchain\cmake\bin\cmake.exe"

if ($Clean -and (Test-Path "$Build\build.ninja")) {
    & $cmake --build $Build --target clean
}
# the launch-time text rendering test is a Debug-only diagnostic: always off here (a cached ON would be kept)
& $cmake -G Ninja -S $RepoRoot -B $Build `
    "-DCMAKE_MAKE_PROGRAM=$Toolchain\ninja\ninja.exe" `
    "-DCMAKE_TOOLCHAIN_FILE=$RepoRoot\cmake\ps4-windows.cmake" `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DPS4IPTV_TEXT_TEST_AT_START=OFF"
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
& $cmake --build $Build
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
Get-ChildItem $Build -Filter *.pkg | Select-Object Name, Length, LastWriteTime
