#!/usr/bin/env bash
# Download integration test on Linux: the counterpart of scripts/run-download-integration-test.ps1. The app's
# libcurl transport + download manager, and M3uService + the app's HTTP client, against the local Python test
# server (scripts/download-test-server.py, standard library only). No real provider is used.
#
# No libcurl development package is needed: the curl 7.80 headers come from the project's PacBrew sysroot (only
# its curl/ directory is put on the include path) and the program links the system's libcurl.so.4 (same SONAME;
# libcurl keeps its ABI backward compatible).
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$repo/build/host-tests-linux"
inc="$out/curl-include"
mkdir -p "$out" "$inc"
ln -sfn "$repo/toolchain/pacbrew/opt/pacbrew/ps4/openorbis/usr/include/curl" "$inc/curl"
libcurl="$(ls /usr/lib/x86_64-linux-gnu/libcurl.so.4 /usr/lib64/libcurl.so.4 /usr/lib/libcurl.so.4 2>/dev/null | head -1 || true)"
[[ -n "$libcurl" ]] || { echo "libcurl.so.4 not found" >&2; exit 1; }

cxx="${CXX:-g++}"
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -pthread
       -I "$repo/src" -I "$inc")
s="$repo/src"
h="$repo/tests/host"
"$cxx" "${flags[@]}" -o "$out/download_it" "$h/test_main.cpp" "$h/download_it.cpp" \
    "$s/downloads/curl_transport.cpp" "$s/downloads/download_manager.cpp" "$s/downloads/download_model.cpp" \
    "$s/downloads/part_file.cpp" "$s/platform/fs.cpp" "$s/platform/log.cpp" "$s/platform/redact.cpp" \
    "$s/core/json.cpp" "$s/core/utf8.cpp" "$s/i18n/i18n.cpp" "$s/i18n/strings_en.cpp" "$s/i18n/strings_tr.cpp" \
    "$libcurl"
"$cxx" "${flags[@]}" -o "$out/m3u_it" "$h/test_main.cpp" "$h/m3u_it.cpp" "$s/app/m3u_service.cpp" "$s/network/http.cpp" "$s/network/http_policy.cpp" \
    "$s/network/jobs.cpp" "$s/iptv/m3u.cpp" "$s/iptv/catalog.cpp" "$s/iptv/search_index.cpp" "$s/iptv/xtream.cpp" \
    "$s/storage/catalog_cache.cpp" "$s/platform/fs.cpp" "$s/platform/log.cpp" "$s/platform/redact.cpp" \
    "$s/platform/clock.cpp" "$s/core/json.cpp" "$s/core/utf8.cpp" "$s/core/url.cpp" "$s/i18n/i18n.cpp" \
    "$s/i18n/strings_en.cpp" "$s/i18n/strings_tr.cpp" "$libcurl"

portfile="$out/download-test-server.port"
rm -f "$portfile"
python3 -I "$repo/scripts/download-test-server.py" "$portfile" &
server=$!
trap 'kill $server 2>/dev/null || true' EXIT
for _ in $(seq 50); do [[ -s "$portfile" ]] && break; sleep 0.1; done
[[ -s "$portfile" ]] || { echo "test server did not start" >&2; exit 1; }
export PS4IPTV_TEST_SERVER="http://127.0.0.1:$(tr -d '[:space:]' < "$portfile")"
export PS4IPTV_TEST_TMP="$out/tmp-it"
mkdir -p "$PS4IPTV_TEST_TMP"
rc=0
"$out/download_it" || rc=1
"$out/m3u_it" || rc=1
exit $rc
