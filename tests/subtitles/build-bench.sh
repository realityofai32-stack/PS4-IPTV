#!/usr/bin/env bash
# Builds tests/subtitles/ass_bench against the host subtitle stack (build-host-ass-stack.sh) and the app's log /
# redaction code. Extra compiler flags (sanitizers) in BENCH_FLAGS. Usage: build-bench.sh OUT_BINARY
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
A="$REPO/build/host-ass"
# shellcheck disable=SC2086
g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers ${BENCH_FLAGS:-} \
    -I "$REPO/src" -I "$A/libass/include" "$REPO/tests/subtitles/ass_bench.cpp" "$REPO/src/platform/log.cpp" \
    "$REPO/src/platform/redact.cpp" "$REPO/src/platform/fs.cpp" "$REPO/src/player/mpv_log_filter.cpp" -o "$1" \
    "$A/libass/lib/libass.a" "$A/fribidi/lib/libfribidi.a" "$A/freetype/lib/libfreetype.a" -lm -pthread
