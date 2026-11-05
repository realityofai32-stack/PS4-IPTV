#!/usr/bin/env bash
# Local Media playback-path test (tests/media/media_it.py) with the FFmpeg 5.0 host build of
# tests/https/build-host-tls-stack.sh and the app's localmedia / protocols code.
set -euo pipefail
export LC_ALL=C
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
"$REPO/tests/https/build-host-tls-stack.sh"
T="$REPO/build/host-tls"
OUT="$REPO/build/media-it"
mkdir -p "$OUT"
FF="$T/ffmpeg"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -pthread ${SANITIZE:-} -DPS4IPTV_FFMPEG_FORMATS \
    -I "$FF/include" -I "$REPO/src" -o "$OUT/media_probe" "$REPO/tests/media/media_probe.cpp" \
    "$REPO/src/media/local_media.cpp" "$REPO/src/media/local_media_ffmpeg.cpp" "$REPO/src/platform/fs.cpp" \
    "$REPO/src/player/protocols.cpp" \
    "$FF/lib/libavformat.a" "$FF/lib/libavcodec.a" "$FF/lib/libswresample.a" "$FF/lib/libavutil.a" \
    "$T/mbedtls/lib/libmbedtls.a" "$T/mbedtls/lib/libmbedx509.a" "$T/mbedtls/lib/libmbedcrypto.a" -lm -ldl
python3 -I "$REPO/tests/media/media_it.py" "$OUT/media_probe" "$OUT/work"
