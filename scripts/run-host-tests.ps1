<#
.SYNOPSIS
    Builds and runs the host-side unit tests with MSVC (/W4 /WX):
      - production app: core (utf8/json/url), iptv (Xtream), storage, keyboard model, redaction,
        DS4 key repeat / stick filter, scroll math, logo pipeline (stb_image from libcross2d), stability
        presets and the reconnect state machine
      - playback test app: redaction and test_streams.txt parsing, plus config_check.exe
#>
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $RepoRoot 'build\host-tests'
New-Item -ItemType Directory -Force $Out | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio / Build Tools not found' }
if (-not (Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include")) {
    throw 'Windows SDK missing (Visual Studio Installer: add Microsoft.VisualStudio.Component.Windows11SDK.26100)'
}
# the x64-hosted toolset may not be installed; vcvarsall x86_amd64 (x86-hosted, x64 target) always is
$vcvars = "`"$(Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat')`" x86_amd64"
$env_prefix = "set `"PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%`" && $vcvars >nul"
$cl = 'cl /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /D_CRT_SECURE_NO_WARNINGS'
# stb_image.h / stb_image_write.h: the copies libcross2d compiles into the PS4 build
$cl += " /I`"$RepoRoot\external\pplay-reference\libcross2d\include`""

function Invoke-TestBuild([string] $name, [string[]] $sources) {
    $obj = Join-Path $Out "obj-$name"
    New-Item -ItemType Directory -Force $obj | Out-Null
    $src = ($sources | ForEach-Object { "`"$_`"" }) -join ' '
    cmd /c "$env_prefix && $cl /Fe`"$Out\$name.exe`" /Fo`"$obj\\`" $src"
    if ($LASTEXITCODE -ne 0) { throw "$name build failed" }
}

# production app
$s = "$RepoRoot\src"
$h = "$RepoRoot\tests\host"
Invoke-TestBuild 'app_tests' @(
    "$h\test_main.cpp", "$h\test_core.cpp", "$h\test_iptv.cpp", "$h\test_storage.cpp", "$h\test_jobs.cpp",
    "$h\test_live.cpp", "$h\test_text.cpp", "$h\test_input.cpp", "$h\test_images.cpp", "$h\test_playback.cpp", "$h\test_vod.cpp",
    "$h\stb_impl.cpp",
    "$s\core\utf8.cpp", "$s\core\json.cpp", "$s\core\url.cpp", "$s\iptv\xtream.cpp", "$s\iptv\catalog.cpp",
    "$s\storage\library_store.cpp", "$s\storage\catalog_cache.cpp",
    "$s\platform\fs.cpp", "$s\platform\redact.cpp", "$s\platform\log.cpp", "$s\network\jobs.cpp",
    "$s\storage\profile_store.cpp", "$s\storage\settings_store.cpp", "$s\ui\keyboard_model.cpp",
    "$s\platform\input_logic.cpp", "$s\ui\scroll_math.cpp", "$s\player\stability.cpp",
    "$s\images\image_key.cpp", "$s\images\disk_cache.cpp", "$s\images\image_decode.cpp",
    "$s\images\image_scheduler.cpp", "$s\images\image_pipeline.cpp")

# playback test app
$t = "$RepoRoot\tests\playback-test"
Invoke-TestBuild 'host_tests' @("$t\host-tests\host_tests.cpp", "$t\src\redact.cpp", "$t\src\stream_config.cpp")
Invoke-TestBuild 'config_check' @("$t\host-tests\config_check.cpp", "$t\src\redact.cpp", "$t\src\stream_config.cpp")

$samples = Join-Path $RepoRoot 'build\xtream-samples'   # optional, from scripts/fetch-xtream-samples.py
if (Test-Path $samples) { $env:PS4IPTV_SAMPLES = $samples } else { Remove-Item Env:\PS4IPTV_SAMPLES -ErrorAction SilentlyContinue }
$icons = Join-Path $RepoRoot 'build\icon-samples'      # optional: real provider logos (never committed)
if (Test-Path $icons) { $env:PS4IPTV_ICON_SAMPLES = $icons } else { Remove-Item Env:\PS4IPTV_ICON_SAMPLES -ErrorAction SilentlyContinue }
$env:PS4IPTV_TEST_TMP = Join-Path $Out 'tmp'
New-Item -ItemType Directory -Force $env:PS4IPTV_TEST_TMP | Out-Null
& "$Out\app_tests.exe"
$appFailed = $LASTEXITCODE -ne 0
& "$Out\host_tests.exe"
$testFailed = $LASTEXITCODE -ne 0
if ($appFailed -or $testFailed) { throw 'host tests failed' }
