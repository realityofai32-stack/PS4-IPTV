<#
.SYNOPSIS
    Download speed benchmark on this PC: the app's real download pipeline (DownloadManager + libcurl 7.80
    CurlTransport + .part writes + manifest, tests/host/download_bench.cpp) against the real provider movie, for
    several socket receive buffer / libcurl buffer sizes. Credentials come from config\test_streams.txt; nothing
    secret is printed. Partial files are deleted. The account allows one connection, so configurations run one
    after another with a pause in between.
.EXAMPLE
    scripts\run-download-bench.ps1 -Id 100491 -Seconds 25 -Configs '32/256','1024/256'
#>
param(
    [string] $Id = '100491',
    [string] $Ext = 'mkv',
    [int] $Seconds = 25,
    [int] $Gap = 45,
    [string[]] $Configs = @('32/256', '64/256', '0/256', '1024/16', '1024/64', '1024/256', '1024/512')
)
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $RepoRoot 'build\host-tests'
$Curl = Join-Path $RepoRoot 'toolchain\host-curl'
New-Item -ItemType Directory -Force $Out | Out-Null
if (-not (Test-Path (Join-Path $RepoRoot 'config\test_streams.txt'))) { throw 'config\test_streams.txt missing' }
if (-not (Test-Path (Join-Path $Curl 'lib\libcurl.lib'))) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'setup-host-curl.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'host libcurl build failed' }
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio / Build Tools not found' }
$vcvars = "set `"PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%`" && `"$(Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat')`" x86_amd64"
$cl = 'cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 /MD /D_CRT_SECURE_NO_WARNINGS /DCURL_STATICLIB'
$cl += " /I`"$Curl\include`""

$s = "$RepoRoot\src"
$sources = @("$RepoRoot\tests\host\download_bench.cpp",
    "$s\downloads\curl_transport.cpp", "$s\downloads\download_manager.cpp", "$s\downloads\download_model.cpp",
    "$s\downloads\part_file.cpp", "$s\platform\fs.cpp", "$s\platform\log.cpp", "$s\platform\redact.cpp",
    "$s\core\json.cpp", "$s\core\utf8.cpp", "$s\i18n\i18n.cpp", "$s\i18n\strings_en.cpp", "$s\i18n\strings_tr.cpp")
$obj = Join-Path $Out 'obj-download_bench'
New-Item -ItemType Directory -Force $obj | Out-Null
$src = ($sources | ForEach-Object { "`"$_`"" }) -join ' '
$libs = "`"$Curl\lib\libcurl.lib`" ws2_32.lib crypt32.lib advapi32.lib wldap32.lib normaliz.lib bcrypt.lib"
cmd /c "$vcvars >nul && $cl /Fe`"$Out\download_bench.exe`" /Fo`"$obj\\`" $src /link $libs"
if ($LASTEXITCODE -ne 0) { throw 'download_bench build failed' }

& "$Out\download_bench.exe" $RepoRoot $Id $Ext $Seconds $Gap @Configs
if ($LASTEXITCODE -ne 0) { throw 'download benchmark failed' }
