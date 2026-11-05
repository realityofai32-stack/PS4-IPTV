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
g++ -std=c++17 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -I "$REPO/src" -o "$OUT/mpv_options" \
    "$REPO/tests/media/mpv_options.cpp" "$REPO/src/player/display.cpp" "$REPO/src/player/tracks.cpp" \
    "$REPO/src/i18n/i18n.cpp" "$REPO/src/i18n/strings_en.cpp" "$REPO/src/i18n/strings_tr.cpp" "$REPO/src/core/utf8.cpp" \
    "$REPO/src/platform/log.cpp" "$REPO/src/platform/redact.cpp" "$REPO/src/platform/fs.cpp" "$REPO/src/platform/clock.cpp" \
    "$REPO/src/core/format.cpp"
# the PS4's mpv 0.34.1 source (options table) for the option-name check; optional
MPV_SRC="$REPO/build/media-it/mpv-0.34.1"
if [[ ! -d "$MPV_SRC" ]]; then
    curl -fsSL --retry 3 -o "$OUT/mpv.tgz" https://github.com/mpv-player/mpv/archive/v0.34.1.tar.gz \
        && echo "32ded8c13b6398310fa27767378193dc1db6d78b006b70dbcbd3123a1445e746  $OUT/mpv.tgz" | sha256sum -c --quiet - \
        && tar xzf "$OUT/mpv.tgz" -C "$OUT" || true
fi
python3 -I "$REPO/tests/media/media_it.py" "$OUT/media_probe" "$OUT/work" "$OUT/mpv_options" "$MPV_SRC"
