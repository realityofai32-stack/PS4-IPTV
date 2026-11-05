#!/usr/bin/env bash
# Host (x86-64 Linux) build of the PS4 HTTPS playback stack for the HTTPS integration tests:
#   - Mbed TLS 2.16.6: the same source archive as PacBrew's ps4-openorbis-mbedtls 2.16.6-3 (sha256 66455e23...1770),
#     whose config.h is byte-identical to the one in the PS4 sysroot (checked below)
#   - FFmpeg 5.0: the same archive and the same configure options as scripts/build-ffmpeg-tls.sh (the PS4 build),
#     except what only concerns the cross build or libraries the host has no headers for (libass, freetype, fribidi,
#     zlib, bzlib: subtitles rendering / compressed metadata, nothing on the network path)
# The PS4 libraries themselves cannot run on Linux; this build runs the same FFmpeg network / HLS / TLS code with the
# same Mbed TLS version and configuration. Output: build/host-tls/{mbedtls,ffmpeg} (git-ignored).
set -euo pipefail
export LC_ALL=C

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TC="$REPO/toolchain"
OUT="$REPO/build/host-tls"
STAMP="$OUT/.stack-ok"
RECIPE="mbedtls-2.16.6+ffmpeg-5.0-tls-host-1"
if [[ -f "$STAMP" && "$(cat "$STAMP")" == "$RECIPE" && "${1:-}" != "--force" ]]; then
    echo "host TLS stack up to date: $OUT"
    exit 0
fi

MBED_ARCHIVE="$TC/downloads/mbedtls-2.16.6-apache.tgz"
FF_ARCHIVE="$TC/downloads/ffmpeg-5.0.tar.xz"
[[ -f "$MBED_ARCHIVE" ]] || curl -fsSL --retry 3 -o "$MBED_ARCHIVE" https://sources.buildroot.net/mbedtls/mbedtls-2.16.6-apache.tgz
echo "66455e23a6190a30142cdc1113f7418158839331a9d8e6b0778631d077281770  $MBED_ARCHIVE" | sha256sum -c --quiet -
echo "51e919f7d205062c0fd4fae6243a84850391115104ccf1efc451733bc0ac7298  $FF_ARCHIVE" | sha256sum -c --quiet -

rm -rf "$OUT"
mkdir -p "$OUT/src"
tar xzf "$MBED_ARCHIVE" -C "$OUT/src"
tar xJf "$FF_ARCHIVE" -C "$OUT/src"

# the PS4 sysroot's Mbed TLS configuration is the upstream default: same TLS behaviour on both
cmp "$OUT/src/mbedtls-2.16.6/include/mbedtls/config.h" \
    "$TC/pacbrew/opt/pacbrew/ps4/openorbis/usr/include/mbedtls/config.h"

"$TC/cmake/bin/cmake" -S "$OUT/src/mbedtls-2.16.6" -B "$OUT/mbedtls-build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$TC/ninja/ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$OUT/mbedtls" \
    -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DCMAKE_C_FLAGS="-fPIC" > "$OUT/mbedtls-configure.log"
"$TC/cmake/bin/cmake" --build "$OUT/mbedtls-build" > "$OUT/mbedtls-build.log"
"$TC/cmake/bin/cmake" --install "$OUT/mbedtls-build" > /dev/null

cd "$OUT/src/ffmpeg-5.0"
# FFmpeg 5.0 predates GCC 14/15 and binutils 2.4x: their new default errors on old C idioms are turned back into
# warnings, and x86 assembly (speed only: codecs) is off
./configure --prefix="$OUT/ffmpeg" \
    --disable-shared --enable-static \
    --disable-programs --disable-debug --disable-doc \
    --disable-avdevice --disable-hwaccels --disable-encoders --disable-muxers \
    --enable-swscale --enable-swresample --enable-network \
    --disable-protocols --enable-protocol=file,http,https,tls,ftp,tcp,udp,rtmp \
    --disable-zlib --disable-bzlib --disable-libass --disable-libfreetype --disable-libfribidi \
    --enable-version3 --enable-mbedtls --disable-x86asm --disable-inline-asm \
    --extra-cflags="-I$OUT/mbedtls/include -Wno-incompatible-pointer-types -Wno-int-conversion" \
    --extra-ldflags="-L$OUT/mbedtls/lib" > "$OUT/ffmpeg-configure.log" 2>&1 \
    || { tail -30 "$OUT/ffmpeg-configure.log"; tail -40 ffbuild/config.log; exit 1; }
for want in CONFIG_HTTPS_PROTOCOL CONFIG_TLS_PROTOCOL CONFIG_MBEDTLS CONFIG_HTTP_PROTOCOL CONFIG_HLS_DEMUXER; do
    grep -q "^#define $want 1" config.h || { echo "configure did not enable $want" >&2; exit 1; }
done
make --quiet -j "$(nproc)" > "$OUT/ffmpeg-make.log" 2>&1 || { tail -40 "$OUT/ffmpeg-make.log"; exit 1; }
make --quiet install > /dev/null
echo "$RECIPE" > "$STAMP"
echo "host TLS stack built: $OUT"
