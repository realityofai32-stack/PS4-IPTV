<#
.SYNOPSIS
    Builds and runs the host-side unit tests (redaction, test_streams.txt parsing) with MSVC.
#>
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $RepoRoot 'build\host-tests'
New-Item -ItemType Directory -Force $Out | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio / Build Tools not found' }
# the x64-hosted toolset may not be installed; vcvarsall x86_amd64 (x86-hosted, x64 target) always is
$vcvars = "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat')`" x86_amd64"
if (-not (Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include")) {
    throw 'Windows SDK missing (Visual Studio Installer: add Microsoft.VisualStudio.Component.Windows11SDK.26100)'
}

$t = "$RepoRoot\tests\playback-test"
$sources = "`"$t\host-tests\host_tests.cpp`" `"$t\src\redact.cpp`" `"$t\src\stream_config.cpp`""
$cmd = "set `"PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%`" && $vcvars >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /Fe`"$Out\host_tests.exe`" /Fo`"$Out\\`" $sources"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw 'host test build failed' }

& "$Out\host_tests.exe"
if ($LASTEXITCODE -ne 0) { throw 'host tests failed' }
