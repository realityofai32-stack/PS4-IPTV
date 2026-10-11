# Building on Linux (developer notes)

Status: verified on **Kubuntu / Ubuntu 26.04** (x86-64). This is a developer note, not yet a supported public
build path; the README's Windows workflow is unchanged.

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

## Commands

```bash
scripts/build-ps4iptv.sh [Release|Debug] [--clean]   # PKG -> build/ps4iptv-release/
scripts/test-host.sh                                  # host unit tests with g++ -Wall -Wextra -Werror
scripts/verify-ps4iptv.sh                             # release validation (build, tests, PKG checks, scans)
```
