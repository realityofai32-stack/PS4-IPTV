<#
.SYNOPSIS
    Builds the unmodified upstream pPlay PS4 target (known-good playback reference) natively on Windows.

.DESCRIPTION
    - clones Cpasjuste/pplay recursively into external\pplay-reference at a pinned commit (if missing)
    - writes the placeholder pscrap\tmdb.key that pscrap's CMakeLists.txt requires (git-ignored
      upstream; TMDB scraping is simply disabled)
    - configures with cmake\ps4-windows.cmake (Windows port of PacBrew's ps4.cmake) and builds with Ninja

    Output: build\pplay-ps4\IV0001-PPLA00001_00-PPLA000013080000.pkg
    Requires scripts\setup-toolchain.ps1 to have been run.
#>
param(
    [string] $BuildType = 'Release'
)

$ErrorActionPreference = 'Stop'

$RepoRoot  = Split-Path -Parent $PSScriptRoot
$Toolchain = Join-Path $RepoRoot 'toolchain'
$Src       = Join-Path $RepoRoot 'external\pplay-reference'
$Build     = Join-Path $RepoRoot 'build\pplay-ps4'
$PplayCommit = '039954636c1f2ef20b5ec188226e7ed245cbb763'   # pplay master, 2026-05-07 (v3.8)

if (-not (Test-Path "$Src\.git")) {
    git clone --recursive https://github.com/Cpasjuste/pplay $Src
    if ($LASTEXITCODE -ne 0) { throw 'git clone failed' }
    git -C $Src checkout --recurse-submodules $PplayCommit
    if ($LASTEXITCODE -ne 0) { throw "git checkout $PplayCommit failed" }
}
$head = git -C $Src rev-parse HEAD
if ($head -ne $PplayCommit) { Write-Warning "pplay-reference is at $head, expected $PplayCommit" }

$tmdbKey = Join-Path $Src 'pscrap\tmdb.key'
if (-not (Test-Path $tmdbKey)) {
    Set-Content -Path $tmdbKey -Value 'NO_TMDB_KEY' -Encoding ascii -NoNewline
}

# Windows PowerShell 5.1 turns native stderr output (e.g. CMake deprecation warnings) into
# terminating errors under 'Stop'; native tools are checked through $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'

$env:OPENORBIS = "$Toolchain\pacbrew\opt\pacbrew\ps4\openorbis" -replace '\\', '/'
$env:OO_PS4_TOOLCHAIN = $env:OPENORBIS
$cmake = "$Toolchain\cmake\bin\cmake.exe"

& $cmake -G Ninja -S $Src -B $Build `
    "-DCMAKE_MAKE_PROGRAM=$Toolchain\ninja\ninja.exe" `
    "-DCMAKE_TOOLCHAIN_FILE=$RepoRoot\cmake\ps4-windows.cmake" `
    -DPLATFORM_PS4=ON `
    "-DCMAKE_BUILD_TYPE=$BuildType"
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

& $cmake --build $Build
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Get-ChildItem $Build -Filter *.pkg | Select-Object Name, Length, LastWriteTime
