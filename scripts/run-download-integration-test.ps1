<#
.SYNOPSIS
    Download integration test: the app's libcurl download transport and download manager against a local HTTP
    test server (scripts/download-test-server.py, Python 3 standard library) - Content-Length, Range / 206,
    If-Range, redirects, interrupted responses with retry, 403 / 404, no Content-Length, a server that ignores
    Range, pause / cancel from another thread, the worker thread and a resume past 4 GiB (sparse file).
    Builds host libcurl 7.80.0 first when needed (scripts/setup-host-curl.ps1). No real provider is used.
#>
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $RepoRoot 'build\host-tests'
$Curl = Join-Path $RepoRoot 'toolchain\host-curl'
New-Item -ItemType Directory -Force $Out | Out-Null

if (-not (Test-Path (Join-Path $Curl 'lib\libcurl.lib'))) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'setup-host-curl.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'host libcurl build failed' }
}
$python = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $python) { throw 'Python 3 is needed for the test server' }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio / Build Tools not found' }
$vcvars = "set `"PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%`" && `"$(Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat')`" x86_amd64"
$cl = 'cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /utf-8 /MD /D_CRT_SECURE_NO_WARNINGS /DCURL_STATICLIB'
$cl += " /I`"$Curl\include`""

$s = "$RepoRoot\src"
$h = "$RepoRoot\tests\host"
$sources = @("$h\test_main.cpp", "$h\download_it.cpp",
    "$s\downloads\curl_transport.cpp", "$s\downloads\download_manager.cpp", "$s\downloads\download_model.cpp",
    "$s\downloads\part_file.cpp", "$s\platform\fs.cpp", "$s\platform\log.cpp", "$s\platform\redact.cpp",
    "$s\core\json.cpp", "$s\core\utf8.cpp", "$s\i18n\i18n.cpp", "$s\i18n\strings_en.cpp", "$s\i18n\strings_tr.cpp")
$obj = Join-Path $Out 'obj-download_it'
New-Item -ItemType Directory -Force $obj | Out-Null
$src = ($sources | ForEach-Object { "`"$_`"" }) -join ' '
$libs = "`"$Curl\lib\libcurl.lib`" ws2_32.lib crypt32.lib advapi32.lib wldap32.lib normaliz.lib bcrypt.lib"
cmd /c "$vcvars >nul && $cl /Fe`"$Out\download_it.exe`" /Fo`"$obj\\`" $src /link $libs"
if ($LASTEXITCODE -ne 0) { throw 'download_it build failed' }

$portFile = Join-Path $Out 'download-test-server.port'
Remove-Item $portFile -ErrorAction SilentlyContinue
$server = Start-Process -FilePath $python.Source -ArgumentList @('-I', "`"$PSScriptRoot\download-test-server.py`"", "`"$portFile`"") -PassThru -WindowStyle Hidden
try {
    for ($i = 0; $i -lt 50 -and -not (Test-Path $portFile); $i++) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path $portFile)) { throw 'test server did not start' }
    $port = (Get-Content $portFile).Trim()
    $env:PS4IPTV_TEST_SERVER = "http://127.0.0.1:$port"
    $env:PS4IPTV_TEST_TMP = Join-Path $Out 'tmp-it'
    New-Item -ItemType Directory -Force $env:PS4IPTV_TEST_TMP | Out-Null
    & "$Out\download_it.exe"
    $failed = $LASTEXITCODE -ne 0
} finally {
    Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
}
if ($failed) { throw 'download integration test failed' }
