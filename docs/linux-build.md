# Building on Linux (Kubuntu)

Status: the release workflow. **V7 IPTV v0.3.0 was built and validated with it** on Kubuntu / Ubuntu 26.04
(x86-64). The Windows / PowerShell workflow in the README is kept for contributors.

## Project-local toolchain (`toolchain/`, git-ignored)

The same component versions as the Windows toolchain (`scripts/setup-toolchain.ps1`), Linux builds:

| Directory | Component | Source archive (in `toolchain/downloads/`) |
|---|---|---|
| `cmake/` | CMake 3.31.12 | `cmake-3.31.12-linux-x86_64.tar.gz` |
| `ninja/` | Ninja 1.13.2 | `ninja-linux-1.13.2.zip` |
| `llvm-12.0.1/` | clang / ld.lld / llvm-ar 12.0.1 | `clang-llvm-12.0.1-linux.tar.xz` |
| `pacbrew/opt/pacbrew/ps4/openorbis/` | PacBrew PS4 sysroot: OpenOrbis headers 0.5.2, musl, libc++ 12, SDL2 2.0.18, libmpv 0.34.1, FFmpeg 5.0, libass, libcurl 7.80 + mbedTLS 2.16.6, ... | `ps4-openorbis-*.pkg.tar.xz` |
| `host-bin/` | `create-fself` (OpenOrbis 1.2 binaries), `create-gp4` (built from source with `toolchain/go`), `PkgTool.Core` (Linux build) | `create-fself-1.2-binaries.tar.gz`, `create-gp4-bf8e66e.zip`, `openorbis-toolchain-v0.5.3-llvm-12.zip` |
| `openssl-1.1.1/` | OpenSSL 1.1.1w shared libraries, **only** for `PkgTool.Core` | `openssl-1.1.1w.tar.gz` |
| `go/` | Go 1.27.1 (host tools: builds `create-gp4` from source) | `go1.27.1.linux-amd64.tar.gz` |

`PkgTool.Core` is a .NET Core 3 single-file application that loads OpenSSL **1.1** at run time; current
distributions ship only OpenSSL 3, so `cmake/ps4-linux.cmake` runs it with
`LD_LIBRARY_PATH=toolchain/openssl-1.1.1/lib` (plus `DOTNET_ROLL_FORWARD=Major`,
`DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1`). Without that the PKG step stops with
"No usable version of libssl was found".

System packages used: `g++` (host tests), `pkgconf`, `python3`, `git`.

## pPlay checkout

V7 IPTV compiles libcross2d and two pPlay sources from `external/pplay-reference` and copies pPlay's PS4 system
modules into the PKG. The checkout is pinned (the same commit `scripts/build-pplay-reference.ps1` uses):

```bash
git clone --recursive https://github.com/Cpasjuste/pplay external/pplay-reference
git -C external/pplay-reference checkout --recurse-submodules 039954636c1f2ef20b5ec188226e7ed245cbb763
```

Release check 8 compares the PKG's linked system modules with a pPlay reference ELF in
`build/pplay-ps4-linux/pplay` (or `build/pplay-ps4/pplay`); without one it reports WARN.

## Commands

```bash
scripts/build-ps4iptv.sh [Release|Debug] [--clean]   # PKG -> build/ps4iptv-release/IV0001-IPTV00002_00-IPTV000020002000.pkg
scripts/test-host.sh                                  # host unit tests with g++ -Wall -Wextra -Werror
scripts/test-download-integration.sh                  # libcurl transport, downloads, M3U service vs a local server
scripts/test-credential-guard.sh                      # self-test of the PKG credential guard
scripts/verify-ps4iptv.sh [Release|Debug]             # release validation (build, tests, PKG checks, scans)
```

Run the scripts with `LC_ALL=C` on systems whose locale uses decimal commas (sorting and number formatting in
the checks expect the C locale).
