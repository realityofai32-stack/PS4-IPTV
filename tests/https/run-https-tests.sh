#!/usr/bin/env bash
# HTTPS playback integration tests: host build of the PS4 TLS stack (tests/https/build-host-tls-stack.sh), the
# harness that opens streams like libmpv 0.34.1 (tests/https/tls_harness.cpp) and local HTTP / HTTPS servers
# (tests/https/https_it.py). --public adds stable public HTTPS test streams (needs Internet).
# Usage: tests/https/run-https-tests.sh [--public]
set -euo pipefail
export LC_ALL=C
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
"$REPO/tests/https/build-host-tls-stack.sh"
T="$REPO/build/host-tls"
OUT="$REPO/build/https-it"
mkdir -p "$OUT"
FF="$T/ffmpeg"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -pthread ${SANITIZE:-} \
    -I "$FF/include" -I "$REPO/src" -o "$OUT/tls_harness" \
    "$REPO/tests/https/tls_harness.cpp" "$REPO/src/platform/redact.cpp" "$REPO/src/player/protocols.cpp" \
    "$FF/lib/libavformat.a" "$FF/lib/libavcodec.a" "$FF/lib/libswresample.a" "$FF/lib/libavutil.a" \
    "$T/mbedtls/lib/libmbedtls.a" "$T/mbedtls/lib/libmbedx509.a" "$T/mbedtls/lib/libmbedcrypto.a" -lm -ldl
python3 -I "$REPO/tests/https/https_it.py" "$OUT/tls_harness" "$OUT/work" "$@"
