<#
.SYNOPSIS
    Sets up the native-Windows PS4 build toolchain in <repo>\toolchain (git-ignored).

.DESCRIPTION
    Everything is downloaded from its upstream source and checked against a pinned SHA-256.
    Nothing is added to PATH and nothing is installed system-wide, except LLVM 12.0.1, whose
    official installer requires administrator rights (one UAC prompt, installed into
    <repo>\toolchain\llvm-12.0.1 with the default "do not modify PATH" option).

    Layout produced:
      toolchain\downloads\        download cache
      toolchain\cmake\            CMake 3.31.12 (3.x on purpose: pPlay uses cmake_minimum_required(VERSION 3.0..3.2))
      toolchain\ninja\            Ninja 1.13.2
      toolchain\pkgconf\          pkgconf 3.0.7 (pkg-config implementation, used by libcross2d's CMake)
      toolchain\go\               Go 1.27.1 (only used to build create-gp4)
      toolchain\llvm-12.0.1\      clang / ld.lld / llvm-ar 12.0.1 (same version PacBrew builds its PS4 libraries with)
      toolchain\host-bin\         create-fself.exe, create-gp4.exe, PkgTool.Core.exe (+ dlls)
      toolchain\pacbrew\opt\pacbrew\ps4\openorbis\   PacBrew PS4 sysroot (= OPENORBIS)

    The PacBrew sysroot keeps its original /opt/pacbrew/... layout so that the unmodified .pc files
    resolve through PKG_CONFIG_SYSROOT_DIR=toolchain\pacbrew.

.PARAMETER SkipLlvm
    Do not run the LLVM installer (for when it is already installed).
#>
param(
    [switch] $SkipLlvm
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$RepoRoot  = Split-Path -Parent $PSScriptRoot
$Toolchain = Join-Path $RepoRoot 'toolchain'
$Downloads = Join-Path $Toolchain 'downloads'
$HostBin   = Join-Path $Toolchain 'host-bin'
New-Item -ItemType Directory -Force $Toolchain, $Downloads, $HostBin | Out-Null

function Get-Verified {
    param([string] $Url, [string] $FileName, [string] $Sha256)
    $dst = Join-Path $Downloads $FileName
    if (Test-Path $dst) {
        if ((Get-FileHash $dst -Algorithm SHA256).Hash -eq $Sha256.ToUpper()) { return $dst }
        Remove-Item $dst
    }
    Write-Host "  downloading $FileName"
    curl.exe -sS -L --fail -o $dst $Url
    if ($LASTEXITCODE -ne 0) { throw "download failed: $Url" }
    $got = (Get-FileHash $dst -Algorithm SHA256).Hash
    if ($got -ne $Sha256.ToUpper()) {
        Remove-Item $dst
        throw "SHA-256 mismatch for $FileName`n  expected $Sha256`n  got      $got"
    }
    return $dst
}

function Expand-Into {
    # Extracts an archive into a fresh temp dir and moves its single top-level folder (or the
    # whole content when -NoStrip) to $Dest.
    param([string] $Archive, [string] $Dest, [switch] $NoStrip)
    $tmp = Join-Path $Toolchain ('_extract_' + [IO.Path]::GetRandomFileName())
    New-Item -ItemType Directory -Force $tmp | Out-Null
    try {
        tar -xf $Archive -C $tmp
        if ($LASTEXITCODE -ne 0) { throw "tar failed: $Archive" }
        if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
        $src = if ($NoStrip) { $tmp } else { (Get-ChildItem $tmp -Directory | Select-Object -First 1).FullName }
        Move-Item $src $Dest
    } finally {
        if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    }
}

# ---------------------------------------------------------------------------------------------
Write-Host '[1/8] CMake 3.31.12'
$cmakeDir = Join-Path $Toolchain 'cmake'
if (-not (Test-Path "$cmakeDir\bin\cmake.exe")) {
    $a = Get-Verified 'https://github.com/Kitware/CMake/releases/download/v3.31.12/cmake-3.31.12-windows-x86_64.zip' `
        'cmake-3.31.12-windows-x86_64.zip' '0c4baa40f28b3f8225eb3fdf6946c987b4fe901403b4eaf2fbbd9378100aaa0c'
    Expand-Into $a $cmakeDir
}

# ---------------------------------------------------------------------------------------------
Write-Host '[2/8] Ninja 1.13.2'
$ninjaDir = Join-Path $Toolchain 'ninja'
if (-not (Test-Path "$ninjaDir\ninja.exe")) {
    $a = Get-Verified 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' `
        'ninja-win-1.13.2.zip' '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
    Expand-Into $a $ninjaDir -NoStrip
}

# ---------------------------------------------------------------------------------------------
Write-Host '[3/8] pkgconf 3.0.7'
$pkgconfDir = Join-Path $Toolchain 'pkgconf'
if (-not (Test-Path "$pkgconfDir\pkgconf.exe")) {
    $a = Get-Verified 'https://files.pythonhosted.org/packages/e2/53/37ee781984b17e16c9b09726c438a327b242721166213411623814b89ddf/pkgconf-3.0.7.post0-py3-none-win_amd64.whl' `
        'pkgconf-3.0.7.post0-py3-none-win_amd64.whl' 'e7906bb5ec1d0d074e397466075a8545f1c50cb11710768644f222ba8acebc7f'
    New-Item -ItemType Directory -Force $pkgconfDir | Out-Null
    tar -xf $a -C $pkgconfDir 'pkgconf/.bin/pkgconf.exe'
    Move-Item "$pkgconfDir\pkgconf\.bin\pkgconf.exe" "$pkgconfDir\pkgconf.exe"
    Remove-Item -Recurse -Force "$pkgconfDir\pkgconf"
}

# ---------------------------------------------------------------------------------------------
Write-Host '[4/8] LLVM 12.0.1'
$llvmDir = Join-Path $Toolchain 'llvm-12.0.1'
if (-not (Test-Path "$llvmDir\bin\clang.exe")) {
    if ($SkipLlvm) { throw "LLVM not found in $llvmDir and -SkipLlvm given" }
    # GPG signature of this file was verified against https://releases.llvm.org/release-keys.asc
    # (good signature, Tom Stellard, key 474E22316ABF4785A88C6E8EA2C794A986419D8A).
    $a = Get-Verified 'https://github.com/llvm/llvm-project/releases/download/llvmorg-12.0.1/LLVM-12.0.1-win64.exe' `
        'LLVM-12.0.1-win64.exe' 'fcbabc9a170208bb344f7bba8366cca57ff103d72a316781bbb77d634b9e9433'
    Write-Host '  running the official installer (administrator / UAC prompt)'
    # NSIS: /S silent, /D=<dir> must be last and unquoted.
    $p = Start-Process -FilePath $a -ArgumentList "/S /D=$llvmDir" -Verb RunAs -Wait -PassThru
    if ($p.ExitCode -ne 0 -or -not (Test-Path "$llvmDir\bin\clang.exe")) {
        throw "LLVM installer failed (exit code $($p.ExitCode))"
    }
}

# ---------------------------------------------------------------------------------------------
Write-Host '[5/8] create-fself 1.2 (OpenOrbis, same version as PacBrew ps4-openorbis-create-fself)'
if (-not (Test-Path "$HostBin\create-fself.exe")) {
    $a = Get-Verified 'https://github.com/OpenOrbis/create-fself/releases/download/v1.2/binaries.tar.gz' `
        'create-fself-1.2-binaries.tar.gz' '2c0678bdf207ee4e5ec20791352b57c2d4a243a2651746b0bd669d10d5fab92f'
    tar -xf $a -C $HostBin --strip-components 2 'bin/windows/create-fself.exe'
}

# ---------------------------------------------------------------------------------------------
Write-Host '[6/8] PkgTool.Core (OpenOrbis toolchain v0.5.3, LLVM-12 build)'
if (-not (Test-Path "$HostBin\PkgTool.Core.exe")) {
    $a = Get-Verified 'https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.3/toolchain-llvm-12.zip' `
        'openorbis-toolchain-v0.5.3-llvm-12.zip' '75d1267d1ec29300e9b578d620d7a55fe678a8c0faeaeeb416a67e6d692352d2'
    $tmp = Join-Path $Toolchain '_oo'
    New-Item -ItemType Directory -Force $tmp | Out-Null
    try {
        tar -xf $a -C $tmp
        $files = 'PkgTool.Core.exe', 'PkgTool.Core.dll', 'PkgTool.Core.runtimeconfig.json',
                 'LibOrbisPkg.Core.dll', 'LibOrbisPkg.dll', 'LICENSE.txt'
        $members = $files | ForEach-Object { "OpenOrbis/PS4Toolchain/bin/windows/$_" }
        tar -xf "$tmp\toolchain-llvm-12.tar.gz" -C $HostBin --strip-components 4 @members
        if ($LASTEXITCODE -ne 0) { throw 'failed to extract PkgTool.Core' }
        Rename-Item "$HostBin\LICENSE.txt" 'LibOrbisPkg-LICENSE.txt' -Force
    } finally {
        Remove-Item -Recurse -Force $tmp
    }
}

# ---------------------------------------------------------------------------------------------
Write-Host '[7/8] create-gp4 @bf8e66e (PacBrew-pinned commit with -path support), built with Go 1.27.1'
if (-not (Test-Path "$HostBin\create-gp4.exe")) {
    $goDir = Join-Path $Toolchain 'go'
    if (-not (Test-Path "$goDir\bin\go.exe")) {
        $a = Get-Verified 'https://go.dev/dl/go1.27.1.windows-amd64.zip' `
            'go1.27.1.windows-amd64.zip' 'a3911b5e0e1b1053f25ed0675f4c1c6aad1e2bfcf253df2b9be4caabd2edd95d'
        Expand-Into $a $goDir
    }
    # same archive + sha256 as PacBrew's ps4-toolchain/create-gp4/PKGBUILD
    $src = Get-Verified 'https://github.com/OpenOrbis/create-gp4/archive/bf8e66e9e9bdd92e41e1697893bc8098a151fa24.zip' `
        'create-gp4-bf8e66e.zip' 'bf089a7c4c301fe93bc1908fd66d35367cd6ac4548dece60096ade3483a06bee'
    $srcDir = Join-Path $Toolchain '_create-gp4-src'
    Expand-Into $src $srcDir
    try {
        $env:GOTOOLCHAIN = 'local'
        $env:GOPATH      = Join-Path $Toolchain '_gopath'
        $env:GOCACHE     = Join-Path $Toolchain '_gocache'
        $env:GOFLAGS     = '-mod=mod'
        Push-Location "$srcDir\cmd\create-gp4"
        & "$goDir\bin\go.exe" build -trimpath -o "$HostBin\create-gp4.exe" .
        if ($LASTEXITCODE -ne 0) { throw 'go build create-gp4 failed' }
    } finally {
        Pop-Location
        Remove-Item -Recurse -Force $srcDir, (Join-Path $Toolchain '_gocache'), (Join-Path $Toolchain '_gopath') -ErrorAction SilentlyContinue
    }
}

# ---------------------------------------------------------------------------------------------
Write-Host '[8/8] PacBrew PS4 sysroot (target libraries only - no Linux host binaries are used)'
$PacBrewRepo = 'https://pacman.mydedibox.fr/pacbrew/packages'
$PacBrewPackages = @(
    # toolchain runtime: OpenOrbis headers/link.x/crt, PacBrew musl, libc++ 12.0.1, Sce stub libs, ps4.cmake
    @{ File = 'ps4-openorbis-headers-0.5.2-4-any.pkg.tar.xz';        Sha256 = '056c7fe8b1874f144d3d67464e96a779ff82a230b3179ba4ec43cd5b392c38d7' }
    @{ File = 'ps4-openorbis-musl-1.5-2-any.pkg.tar.xz';             Sha256 = '9efb4e1c02b2004a79438b106e36bf0d311669cfcfdaa669d46caea462087f3f' }
    @{ File = 'ps4-openorbis-libcxx-12.0.1-1-any.pkg.tar.xz';        Sha256 = '26a230e2d53d1ed11732242e7c131d87719d187f5a6f3632c3c4786bf14d3416' }
    @{ File = 'ps4-openorbis-orbis-lib-gen-1.3-2-any.pkg.tar.xz';    Sha256 = '5ea71a7137a1a98eac09e33c4ed36806ecdf1412a4233e1eb0745e497899ec52' }
    @{ File = 'ps4-openorbis-vars-1.1-3-any.pkg.tar.xz';             Sha256 = 'ad2353896b1a63e72e52ce52ff490f90682a0e3c24f1a98229881f685ac3fbcf' }
    # portlibs used by pPlay / libcross2d / pscrap
    @{ File = 'ps4-openorbis-glm-0.9.9.8-2-any.pkg.tar.xz';          Sha256 = '63d797098ce710364067e7c0d1ab965fd578930fffd2a293bc545807e3a37e62' }
    @{ File = 'ps4-openorbis-zlib-1.3.1-2-any.pkg.tar.xz';           Sha256 = '2b082d25f34d923e2fd35e0b5f29b19e142e263da4380665bb0e80f0a6378700' }
    @{ File = 'ps4-openorbis-bzip2-1.0.6-3-any.pkg.tar.xz';          Sha256 = 'de5e4b3441326b47c9e57693d7e86bc837070c6af4581d60ccdbdb6230f3ffcb' }
    @{ File = 'ps4-openorbis-libpng-1.6.37-3-any.pkg.tar.xz';        Sha256 = 'abf49d48a24397c6a2f163fd60344458b662c9411eec215830c4b3d464d0cbb2' }
    @{ File = 'ps4-openorbis-freetype-2.10.1-3-any.pkg.tar.xz';      Sha256 = 'a084e5b84f3c39d5bd59ac53dbfdc97f7d74cdad9f439ec4454f8d57c134d6e3' }
    @{ File = 'ps4-openorbis-libfribidi-1.0.4-2-any.pkg.tar.xz';     Sha256 = 'e236677e157bb47d9263be2a309495c124dd0c332b53e01e97d643ba24ab9cc1' }
    @{ File = 'ps4-openorbis-libass-0.14.0-2-any.pkg.tar.xz';        Sha256 = '4dc0c2736be2970f961f781960ff3e80cdcfc4a88ac1d382b1a15f7d55c36ebf' }
    @{ File = 'ps4-openorbis-ffmpeg-5.0-5-any.pkg.tar.xz';           Sha256 = '84ac95ed3f66aead48b21ec8b2213a58c06c82e0168450fae9e0e49eba7aecfb' }
    @{ File = 'ps4-openorbis-libmpv-0.34.1-6-any.pkg.tar.xz';        Sha256 = '743d6ef7ef37d94e9aa04ce86d928f7d5fb65ead8ea901191879227f07946abd' }
    @{ File = 'ps4-openorbis-sdl2-2.0.18-16-any.pkg.tar.xz';         Sha256 = 'd447992bfe198bbe08230fbeb512e30d90aa3e03f0561e2041fe2c4d7da32bb2' }
    @{ File = 'ps4-openorbis-libsamplerate-0.1.9-2-any.pkg.tar.xz';  Sha256 = '2000952080e06210a6a2db891d84ada45e4a52b5063cecd626c82faa11ce2f79' }
    @{ File = 'ps4-openorbis-libopus-1.3-2-any.pkg.tar.xz';          Sha256 = '30294216af18d0ac759f3b59ccf5eb2538dab9acb02a93d0d458a36432debfce' }
    @{ File = 'ps4-openorbis-libconfig-1.7.3-3-any.pkg.tar.xz';      Sha256 = 'e5576042b5f52b8a98eae1f4a7aadde117d040fa7118b08823b49f410130c05a' }
    @{ File = 'ps4-openorbis-libcurl-7.80.0-3-any.pkg.tar.xz';       Sha256 = '249c987a8e41e1b7d320cd3129aa1752416dc19e830714d5a38be6c977f5db98' }
    @{ File = 'ps4-openorbis-libjson-c-0.15-2-any.pkg.tar.xz';       Sha256 = '4ea32ab597afdfded04a0bf873d55d116fa4fd1cf9c45bd6eaa1604e9fda7979' }
    @{ File = 'ps4-openorbis-mbedtls-2.16.6-3-any.pkg.tar.xz';       Sha256 = '7fe7ad512c2ccf7b53865433753b9ddcaf6385eed36b8160d93134fa64f0dc58' }
)
$sysroot  = Join-Path $Toolchain 'pacbrew'
$stamp    = Join-Path $sysroot '.installed'
$wanted   = ($PacBrewPackages | ForEach-Object { $_.File }) -join "`n"
$current  = if (Test-Path $stamp) { Get-Content $stamp -Raw } else { '' }
if ($current.Trim() -ne $wanted.Trim()) {
    if (Test-Path $sysroot) { Remove-Item -Recurse -Force $sysroot }
    New-Item -ItemType Directory -Force $sysroot | Out-Null
    foreach ($pkg in $PacBrewPackages) {
        $a = Get-Verified "$PacBrewRepo/$($pkg.File)" $pkg.File $pkg.Sha256
        tar -xf $a -C $sysroot --exclude '.PKGINFO' --exclude '.BUILDINFO' --exclude '.MTREE'
        if ($LASTEXITCODE -ne 0) { throw "failed to extract $($pkg.File)" }
    }
    Set-Content -Path $stamp -Value $wanted -Encoding ascii
}

# ---------------------------------------------------------------------------------------------
Write-Host ''
Write-Host 'Toolchain ready:'
& "$cmakeDir\bin\cmake.exe" --version | Select-Object -First 1
'ninja ' + (& "$ninjaDir\ninja.exe" --version)
'pkgconf ' + (& "$pkgconfDir\pkgconf.exe" --version)
& "$llvmDir\bin\clang.exe" --version | Select-Object -First 1
& "$llvmDir\bin\ld.lld.exe" --version
"OPENORBIS = $sysroot\opt\pacbrew\ps4\openorbis"
