#!/usr/bin/env bash
# Rebuild the PS4 FFmpeg 5.0 static libraries with HTTPS (TLS over mbedTLS) for the playback stack.
#
# The recipe is PacBrew's ps4-openorbis-ffmpeg 5.0-5 (PKGBUILD sha256 c37f761b...b31e6, the exact build that is in
# the sysroot; its configure line is embedded in libavutil.a as avutil_configuration). Only these changes:
#
#   --enable-protocol=...,https,tls   HTTPS and the TLS layer under it (HLS segments, redirects and keys reuse them)
#   --enable-mbedtls                  TLS backend: the Mbed TLS 2.16.6 already linked for libcurl (same library,
#                                     same CA bundle, same platform entropy path that libcurl uses on the console)
#   --enable-version3                 required by FFmpeg for mbedTLS (Apache-2.0); V7 IPTV is GPL-3.0-or-later
#
# Every other option (codecs, demuxers, protocols, filters) is unchanged, and it is the same FFmpeg version, so the
# prebuilt libmpv 0.34.1 links against the result without being rebuilt: its stream_lavf/demux_lavf already pass
# tls_verify / ca_file / user_agent to FFmpeg, including nested HLS segment opens (demuxer-lavf-propagate-opts).
#
# Output: toolchain/ffmpeg-5.0-tls/{include,lib} (git-ignored). CMakeLists.txt links these libraries in place of the
# sysroot's libav*.a. The sysroot itself is not modified.
#
# Usage: scripts/build-ffmpeg-tls.sh [--force]
set -euo pipefail
export LC_ALL=C

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TC="$REPO/toolchain"
OPENORBIS="$TC/pacbrew/opt/pacbrew/ps4/openorbis"
LLVM="$TC/llvm-12.0.1/bin"
# x86 SIMD (the PacBrew build has it: ~500 SSE2/AVX2 entry points in libavcodec.a). Project-local NASM from the
# distribution package (apt-get download nasm; dpkg-deb -x ... toolchain/nasm), or any nasm on PATH.
NASM_DIR="$TC/nasm/usr/bin"
ARCHIVE="$TC/downloads/ffmpeg-5.0.tar.xz"
ARCHIVE_SHA256="51e919f7d205062c0fd4fae6243a84850391115104ccf1efc451733bc0ac7298"
SRC_ROOT="$TC/src/ffmpeg-tls"
PREFIX="$TC/ffmpeg-5.0-tls"
STAMP="$PREFIX/.build-ok"
RECIPE_ID="pacbrew-ffmpeg-5.0-5+tls-mbedtls-1"

if [[ "${1:-}" != "--force" && -f "$STAMP" && "$(cat "$STAMP")" == "$RECIPE_ID" ]]; then
    echo "FFmpeg TLS build up to date: $PREFIX"
    exit 0
fi

for p in "$OPENORBIS/link.x" "$LLVM/clang" "$OPENORBIS/usr/lib/libmbedtls.a" "$OPENORBIS/usr/include/mbedtls/ssl.h"; do
    [[ -e "$p" ]] || { echo "missing $p - see docs/linux-build.md" >&2; exit 1; }
done

if [[ ! -f "$ARCHIVE" ]]; then
    echo "downloading FFmpeg 5.0 source"
    curl -fsSL --retry 3 -o "$ARCHIVE.part" https://ffmpeg.org/releases/ffmpeg-5.0.tar.xz
    mv "$ARCHIVE.part" "$ARCHIVE"
fi
echo "$ARCHIVE_SHA256  $ARCHIVE" | sha256sum -c --quiet -

rm -rf "$SRC_ROOT" "$PREFIX"
mkdir -p "$SRC_ROOT"
tar xJf "$ARCHIVE" -C "$SRC_ROOT"
cd "$SRC_ROOT/ffmpeg-5.0"

# pkg-config: equivalent of PacBrew's openorbis-pkg-config wrapper (same mapping as cmake/ps4-linux.cmake)
cat > "$SRC_ROOT/openorbis-pkg-config" <<EOF
#!/bin/sh
PKG_CONFIG_DIR= PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$OPENORBIS/usr/lib/pkgconfig" \\
PKG_CONFIG_SYSROOT_DIR="$TC/pacbrew" exec /usr/bin/pkgconf --static "\$@"
EOF
chmod +x "$SRC_ROOT/openorbis-pkg-config"

# ps4vars.sh (PacBrew ps4-openorbis-vars 1.1-3) with the project-local paths
export CC="$LLVM/clang" CXX="$LLVM/clang++" AS="$LLVM/clang" LD="$LLVM/ld.lld" AR="$LLVM/llvm-ar"
export RANLIB="$LLVM/llvm-ranlib" NM="$LLVM/llvm-nm" STRIP="$LLVM/llvm-strip"
ARCH="--target=x86_64-pc-freebsd12-elf"
export CFLAGS="$ARCH -D__PS4__ -D__OPENORBIS__ -D__ORBIS__ -DPS4 -D__BSD_VISIBLE -D_BSD_SOURCE -O2 -fPIC -funwind-tables"
export CFLAGS="$CFLAGS -isysroot $OPENORBIS -isystem $OPENORBIS/include -I$OPENORBIS/usr/include"
export CXXFLAGS="$CFLAGS -isystem $OPENORBIS/include/c++/v1"
export CPPFLAGS="$CFLAGS -isystem $OPENORBIS/include/c++/v1"
LIBS="-L$OPENORBIS/lib -L$OPENORBIS/usr/lib -lc -lclang_rt.builtins-x86_64 -lkernel -lSceLibcInternal"
export LDFLAGS="$ARCH -fuse-ld=lld -Wl,-melf_x86_64 -Wl,-pie -Wl,--script=$OPENORBIS/link.x -Wl,--eh-frame-hdr $LIBS -nostdlib"
export PATH="$LLVM:$NASM_DIR:$PATH"
command -v nasm >/dev/null || { echo "nasm not found (toolchain/nasm/usr/bin or PATH)" >&2; exit 1; }

./configure --prefix="$PREFIX" \
    --enable-cross-compile --cross-prefix="$LLVM/" \
    --arch=x86_64 --target-os=freebsd \
    --disable-shared --enable-static \
    --pkg-config="$SRC_ROOT/openorbis-pkg-config" \
    --disable-programs --disable-debug --disable-doc \
    --disable-avdevice --disable-hwaccels --disable-encoders --disable-muxers \
    --enable-swscale --enable-swresample --enable-network \
    --disable-protocols --enable-protocol=file,http,https,tls,ftp,tcp,udp,rtmp \
    --enable-zlib --enable-bzlib --enable-libass --enable-libfreetype --enable-libfribidi \
    --enable-version3 --enable-mbedtls \
    --cc="$CC" --cxx="$CXX" --nm="$NM" --strip="$STRIP" --ar="$AR" --ranlib="$RANLIB" \
    > "$SRC_ROOT/configure.log" 2>&1 || { tail -40 "$SRC_ROOT/configure.log"; tail -60 ffbuild/config.log; exit 1; }

# PacBrew: "TODO: fix this"
sed -i 's|#define HAVE_SYSCTL 1|//#define HAVE_SYSCTL 1|g' config.h
# The configure line is embedded in every library (av*_configuration) and so in eboot.bin: keep the build
# machine's absolute paths (user name, home directory) out of the PKG. Sources are compiled in-tree with relative
# paths, so __FILE__ strings carry no host path either (checked below).
sed -i "s|$TC/|toolchain/|g" config.h

for want in CONFIG_HTTPS_PROTOCOL CONFIG_TLS_PROTOCOL CONFIG_MBEDTLS CONFIG_HTTP_PROTOCOL CONFIG_HLS_DEMUXER; do
    grep -q "^#define $want 1" config.h || { echo "configure did not enable $want" >&2; exit 1; }
done

make --quiet -j "$(getconf _NPROCESSORS_ONLN)" > "$SRC_ROOT/make.log" 2>&1 || { tail -40 "$SRC_ROOT/make.log"; exit 1; }
make --quiet install > "$SRC_ROOT/install.log" 2>&1
if grep -l -a -F "$REPO" "$PREFIX"/lib/*.a; then echo "host path embedded in the libraries above" >&2; exit 1; fi
rm -rf "$PREFIX/share"
cp config.h "$PREFIX/ffmpeg-config.h"
echo "$RECIPE_ID" > "$STAMP"
echo "FFmpeg 5.0 + TLS (mbedTLS) installed to $PREFIX"
