#!/usr/bin/env bash
# Builds the PS4 IPTV production PKG (title IPTV00002) natively on Linux: the counterpart of
# scripts/build-ps4iptv.ps1. configure -> compile -> link -> fself (eboot.bin) -> credential guard -> SFO -> GP4
# -> PKG, with cmake/ps4-linux.cmake and the project-local toolchain (toolchain/, see docs/linux-build.md).
# Output: build/ps4iptv-<type>/IV0001-IPTV00002_00-IPTV0000200xx000.pkg
# Usage: scripts/build-ps4iptv.sh [Release|Debug] [--clean]
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
type="Release"
clean=0
for a in "$@"; do
    case "$a" in
        Release|Debug) type="$a" ;;
        --clean) clean=1 ;;
        *) echo "usage: $0 [Release|Debug] [--clean]" >&2; exit 2 ;;
    esac
done
tc="$repo/toolchain"
build="$repo/build/ps4iptv-${type,,}"

[[ -f "$repo/external/pplay-reference/libcross2d/CMakeLists.txt" ]] \
    || { echo "pPlay checkout missing: external/pplay-reference (see docs/linux-build.md)" >&2; exit 1; }
for f in "$tc/cmake/bin/cmake" "$tc/ninja/ninja"; do
    [[ -x "$f" ]] || { echo "missing $f (see docs/linux-build.md)" >&2; exit 1; }
done

export OPENORBIS="$tc/pacbrew/opt/pacbrew/ps4/openorbis"
export OO_PS4_TOOLCHAIN="$OPENORBIS"
cmake="$tc/cmake/bin/cmake"

if (( clean )) && [[ -f "$build/build.ninja" ]]; then
    "$cmake" --build "$build" --target clean
fi
# the launch-time text rendering test is a Debug-only diagnostic: always off here (a cached ON would be kept)
"$cmake" -G Ninja -S "$repo" -B "$build" \
    -DCMAKE_MAKE_PROGRAM="$tc/ninja/ninja" \
    -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/ps4-linux.cmake" \
    -DCMAKE_BUILD_TYPE="$type" \
    -DPS4IPTV_TEXT_TEST_AT_START=OFF
"$cmake" --build "$build"
ls -l "$build"/*.pkg
