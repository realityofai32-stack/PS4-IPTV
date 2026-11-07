#!/usr/bin/env bash
# Every automated test of the v7-next-features work, in one run (results in build/feature-tests/summary.txt):
#   host unit tests (g++ -Werror) and the same under ASan + UBSan, HTTPS playback integration (host FFmpeg 5.0 +
#   Mbed TLS 2.16.6 build), Stalker / MAG integration (mock portals), Local Media playback path, fuzzing (libFuzzer +
#   ASan replay), download / M3U integration, credential guard self-test, and the PS4 PKG inspection (TLS in the
#   shipped eboot.bin). Usage: tests/run-feature-tests.sh [--public] [fuzz seconds per target, default 60]
set -uo pipefail
export LC_ALL=C
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO/build/feature-tests"
mkdir -p "$OUT"
PUBLIC=""
FUZZ=60
for a in "$@"; do
    case "$a" in --public) PUBLIC="--public" ;; *) FUZZ="$a" ;; esac
done
summary="$OUT/summary.txt"
: > "$summary"
# every suite runs under a time limit (SUITE_TIMEOUT seconds, default 1800; fuzzing gets its fuzz time on top), so
# a hung suite is reported as TIMEOUT instead of blocking the whole run
SUITE_TIMEOUT="${SUITE_TIMEOUT:-1800}"
run() {
    local name="$1"; shift
    local log="$OUT/$name.log"
    local limit="$SUITE_TIMEOUT"
    [[ "$name" == fuzzing ]] && limit=$((SUITE_TIMEOUT + 10 * FUZZ))
    timeout --kill-after=30 "$limit" "$@" > "$log" 2>&1
    local rc=$?
    if (( rc == 0 )); then r=PASS; elif (( rc == 124 || rc == 137 )); then r=FAIL; echo "TIMEOUT after ${limit}s" >> "$log"; else r=FAIL; fi
    local detail
    detail="$(grep -E "checks, [0-9]+ failures|local case|failure\(s\)|^ok +fuzz|replayed|input\(s\)|passed, " "$log" | tr '\n' ' ' | cut -c1-300)"
    grep -q "^TIMEOUT after" "$log" && detail="TIMEOUT after ${limit}s $detail"
    printf '%s  %-26s %s\n' "$r" "$name" "$detail" | tee -a "$summary"
}
run host-unit "$REPO/scripts/test-host.sh"
run host-unit-sanitized "$REPO/scripts/test-host-sanitize.sh"
run https-integration "$REPO/tests/https/run-https-tests.sh" $PUBLIC
run stalker-integration "$REPO/tests/stalker/run-stalker-it.sh"
run local-media-playback "$REPO/tests/media/run-media-tests.sh"
run download-m3u-integration "$REPO/scripts/test-download-integration.sh"
run credential-guard "$REPO/scripts/test-credential-guard.sh"
run fuzzing "$REPO/tests/fuzz/run-fuzz.sh" "$FUZZ"
run subtitles "$REPO/tests/subtitles/run-subtitle-tests.sh"
run network-failures "$REPO/tests/network/run-network-tests.sh"
run version "$REPO/tests/version/test-app-version.sh"
pkg="$REPO/build/ps4iptv-release/IV0001-IPTV00002_00-IPTV000020002000.pkg"
if [[ -f "$pkg" ]]; then
    run ps4-pkg-tls-inspection python3 -I "$REPO/tests/https/inspect_ps4_binary.py" "$REPO/build/ps4iptv-release" "$pkg" "$OUT"
fi
! grep -q '^FAIL' "$summary"
