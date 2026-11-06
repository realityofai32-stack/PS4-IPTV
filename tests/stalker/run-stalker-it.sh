#!/usr/bin/env bash
# Stalker / MAG integration test: the app's Stalker client (StalkerService, SourceService, VodLibrary, next-episode
# queue, libcurl HTTP client) against the mock portals of tests/stalker/mock_portal.py. Synthetic data only.
# Like scripts/test-download-integration.sh: curl 7.80 headers from the PacBrew sysroot, the system libcurl.so.4.
set -euo pipefail
export LC_ALL=C
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="$repo/build/host-tests-linux"
inc="$out/curl-include"
mkdir -p "$out" "$inc"
ln -sfn "$repo/toolchain/pacbrew/opt/pacbrew/ps4/openorbis/usr/include/curl" "$inc/curl"
libcurl="$(ls /usr/lib/x86_64-linux-gnu/libcurl.so.4 /usr/lib64/libcurl.so.4 /usr/lib/libcurl.so.4 2>/dev/null | head -1 || true)"
s="$repo/src"
h="$repo/tests/host"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -pthread \
    ${SANITIZE:-} -I "$s" -I "$inc" -o "$out/stalker_it" "$h/test_main.cpp" "$h/stalker_it.cpp" \
    "$s/app/stalker_service.cpp" "$s/app/xtream_service.cpp" "$s/app/vod_library.cpp" "$s/app/auto_next.cpp" \
    "$s/app/series_plan.cpp" "$s/iptv/stalker.cpp" "$s/iptv/xtream.cpp" "$s/iptv/catalog.cpp" \
    "$s/iptv/search_index.cpp" "$s/network/http.cpp" "$s/network/http_policy.cpp" "$s/network/jobs.cpp" "$s/storage/catalog_cache.cpp" \
    "$s/storage/library_store.cpp" "$s/platform/fs.cpp" "$s/platform/log.cpp" "$s/platform/redact.cpp" \
    "$s/platform/clock.cpp" "$s/player/protocols.cpp" "$s/core/json.cpp" "$s/core/utf8.cpp" "$s/core/url.cpp" \
    "$s/core/hash.cpp" "$s/core/format.cpp" "$s/i18n/i18n.cpp" "$s/i18n/strings_en.cpp" "$s/i18n/strings_tr.cpp" "$libcurl"
ports="$out/stalker-mock.ports"
rm -f "$ports"
python3 -I "$repo/tests/stalker/mock_portal.py" "$ports" &
server=$!
trap 'kill $server 2>/dev/null || true' EXIT
for _ in $(seq 50); do [[ -s "$ports" ]] && break; sleep 0.1; done
[[ -s "$ports" ]] || { echo "mock portal did not start" >&2; exit 1; }
export PS4IPTV_STALKER_PORTS="$ports"
export PS4IPTV_TEST_TMP="$out/tmp-stalker-it"
rm -rf "$PS4IPTV_TEST_TMP"
mkdir -p "$PS4IPTV_TEST_TMP"
"$out/stalker_it"
