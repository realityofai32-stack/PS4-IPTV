<#
.SYNOPSIS
    Builds the PS4 IPTV playback test PKG (title IPTT00001) natively on Windows.

.DESCRIPTION
    Requires scripts\setup-toolchain.ps1 and scripts\build-pplay-reference.ps1 (for the pPlay checkout
    the test builds against). Output: build\playback-test\IV0001-IPTT00001_00-IPTT000010100000.pkg
#>
param(
    [string] $BuildType = 'Release',
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'

$RepoRoot  = Split-Path -Parent $PSScriptRoot
$Toolchain = Join-Path $RepoRoot 'toolchain'
$Src       = Join-Path $RepoRoot 'tests\playback-test'
$Build     = Join-Path $RepoRoot 'build\playback-test'

if (-not (Test-Path "$RepoRoot\external\pplay-reference\libcross2d\CMakeLists.txt")) {
    throw 'pPlay checkout missing: run scripts\build-pplay-reference.ps1 first'
}

# Windows PowerShell 5.1 turns native stderr output into terminating errors under 'Stop';
# native tools are checked through $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'

$env:OPENORBIS = "$Toolchain\pacbrew\opt\pacbrew\ps4\openorbis" -replace '\\', '/'
$env:OO_PS4_TOOLCHAIN = $env:OPENORBIS
$cmake = "$Toolchain\cmake\bin\cmake.exe"

if ($Clean -and (Test-Path "$Build\build.ninja")) {
    & $cmake --build $Build --target clean
}

& $cmake -G Ninja -S $Src -B $Build `
    "-DCMAKE_MAKE_PROGRAM=$Toolchain\ninja\ninja.exe" `
    "-DCMAKE_TOOLCHAIN_FILE=$RepoRoot\cmake\ps4-windows.cmake" `
    "-DCMAKE_BUILD_TYPE=$BuildType"
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

& $cmake --build $Build
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Get-ChildItem $Build -Filter *.pkg | Select-Object Name, Length, LastWriteTime
