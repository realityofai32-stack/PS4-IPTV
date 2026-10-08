<#
.SYNOPSIS
    Builds libcurl 7.80.0 - the version the PS4 build links (PacBrew ps4-openorbis-libcurl 7.80.0) - as a
    static MSVC library (/MD runtime) for the host download integration test (scripts/run-download-integration-test.ps1).

    Source: curl-7.80.0.tar.xz, verified against the sha256 of the PacBrew recipe. HTTP only (the test server
    runs on 127.0.0.1), no TLS / LDAP / IDN / zlib. Output (git-ignored): toolchain\host-curl\{include,lib}.
    Needs the project toolchain (scripts\setup-toolchain.ps1: CMake, Ninja) and Visual Studio Build Tools.
#>
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Toolchain = Join-Path $RepoRoot 'toolchain'
$Downloads = Join-Path $Toolchain 'downloads'
$Prefix = Join-Path $Toolchain 'host-curl'
$Version = '7.80.0'
$Sha256 = 'a132bd93188b938771135ac7c1f3ac1d3ce507c1fcbef8c471397639214ae2ab'   # PacBrew libcurl PKGBUILD

if (Test-Path (Join-Path $Prefix 'lib\libcurl.lib')) {
    Write-Host "host libcurl $Version already built: $Prefix"
    exit 0
}

New-Item -ItemType Directory -Force $Downloads | Out-Null
$archive = Join-Path $Downloads "curl-$Version.tar.xz"
if (-not (Test-Path $archive)) {
    Write-Host "downloading curl $Version"
    Invoke-WebRequest -Uri "https://curl.se/download/curl-$Version.tar.xz" -OutFile $archive -UseBasicParsing
}
$hash = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLower()
if ($hash -ne $Sha256) {
    Remove-Item $archive
    throw "curl-$Version.tar.xz sha256 mismatch: $hash"
}

$src = Join-Path $Toolchain "host-curl-src"
if (Test-Path $src) { Remove-Item -Recurse -Force $src }
New-Item -ItemType Directory -Force $src | Out-Null
tar -xf $archive -C $src
if ($LASTEXITCODE -ne 0) { throw 'extracting curl failed' }
$srcDir = Join-Path $src "curl-$Version"

$cmake = Get-ChildItem -Recurse -Filter cmake.exe (Join-Path $Toolchain 'cmake') | Select-Object -First 1
$ninja = Get-ChildItem -Recurse -Filter ninja.exe (Join-Path $Toolchain 'ninja') | Select-Object -First 1
if (-not $cmake -or -not $ninja) { throw 'CMake / Ninja missing: run scripts\setup-toolchain.ps1 first' }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio / Build Tools not found' }
$vcvars = "set `"PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%`" && `"$(Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat')`" x86_amd64"

$build = Join-Path $src 'build'
$cfg = @(
    "-S `"$srcDir`"", "-B `"$build`"", '-G Ninja', "-DCMAKE_MAKE_PROGRAM=`"$($ninja.FullName)`"",
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_INSTALL_PREFIX=`"$Prefix`"",
    '-DBUILD_SHARED_LIBS=OFF', '-DBUILD_CURL_EXE=OFF', '-DBUILD_TESTING=OFF', '-DHTTP_ONLY=ON',
    '-DCURL_ENABLE_SSL=OFF', '-DCMAKE_USE_SCHANNEL=OFF', '-DCMAKE_USE_OPENSSL=OFF', '-DCURL_USE_LIBSSH2=OFF',
    '-DCURL_ZLIB=OFF', '-DUSE_LIBIDN2=OFF', '-DUSE_WIN32_IDN=OFF', '-DCURL_DISABLE_LDAP=ON', '-DENABLE_UNICODE=OFF'
) -join ' '
cmd /c "$vcvars >nul && `"$($cmake.FullName)`" $cfg"
if ($LASTEXITCODE -ne 0) { throw 'curl configure failed' }
cmd /c "$vcvars >nul && `"$($cmake.FullName)`" --build `"$build`" && `"$($cmake.FullName)`" --install `"$build`""
if ($LASTEXITCODE -ne 0) { throw 'curl build failed' }
Write-Host "host libcurl $Version installed in $Prefix"
