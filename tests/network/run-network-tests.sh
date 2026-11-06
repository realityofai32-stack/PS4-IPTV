#!/usr/bin/env bash
# Network failure simulation for the app's HTTP client (network/http.cpp + http_policy): temporary 5xx answers,
# connection resets, partial answers, stalled and slow servers, refused connections, unknown hosts, cancellation,
# redirect loops, redirects to other schemes, https -> http downgrades, certificate errors, portal header leaks,
# size limits and parallel canceled jobs - against tests/network/failure_server.py (local only). Built plain and
# with ASan + UBSan (system libcurl.so.4 with the PacBrew 7.80 headers, like scripts/test-download-integration.sh).
# Usage: tests/network/run-network-tests.sh
set -euo pipefail
export LC_ALL=C
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$REPO/build/network-tests"
mkdir -p "$OUT"
inc="$OUT/curl-include"
mkdir -p "$inc"
ln -sfn "$REPO/toolchain/pacbrew/opt/pacbrew/ps4/openorbis/usr/include/curl" "$inc/curl"
libcurl="$(ls /usr/lib/x86_64-linux-gnu/libcurl.so.4 /usr/lib64/libcurl.so.4 /usr/lib/libcurl.so.4 2>/dev/null | head -1 || true)"
[[ -n "$libcurl" ]] || { echo "libcurl.so.4 not found" >&2; exit 1; }

# test CA + a certificate for localhost / 127.0.0.1
c="$OUT/certs"
rm -rf "$c"; mkdir -p "$c"
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$c/ca.key" -out "$c/ca.crt" -days 2 -subj "/CN=V7 test CA" \
    > /dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -keyout "$c/leaf.key" -out "$c/leaf.csr" -subj "/CN=localhost" > /dev/null 2>&1
printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' > "$c/ext"
openssl x509 -req -in "$c/leaf.csr" -CA "$c/ca.crt" -CAkey "$c/ca.key" -CAcreateserial -out "$c/leaf.crt" -days 2 \
    -extfile "$c/ext" > /dev/null 2>&1

s="$REPO/src"
srcs=("$REPO/tests/host/test_main.cpp" "$REPO/tests/network/http_it.cpp" "$s/network/http.cpp" "$s/network/http_policy.cpp"
      "$s/network/jobs.cpp" "$s/platform/clock.cpp" "$s/platform/log.cpp" "$s/platform/redact.cpp" "$s/platform/fs.cpp"
      "$s/i18n/i18n.cpp" "$s/i18n/strings_en.cpp" "$s/i18n/strings_tr.cpp")
flags=(-std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -pthread
       -I "$s" -I "$inc")
g++ "${flags[@]}" -o "$OUT/http_it" "${srcs[@]}" "$libcurl"
g++ "${flags[@]}" -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -o "$OUT/http_it_asan" "${srcs[@]}" "$libcurl"

portfile="$OUT/ports"
rm -f "$portfile"
python3 -I "$REPO/tests/network/failure_server.py" "$portfile" "$c/leaf.crt" "$c/leaf.key" &
server=$!
trap 'kill $server 2>/dev/null || true' EXIT
for _ in $(seq 100); do [[ -s "$portfile" ]] && break; sleep 0.1; done
read -r hp sp < "$portfile"
closed="$(python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')"
export PS4IPTV_NET_HTTP="http://127.0.0.1:$hp" PS4IPTV_NET_HTTPS="https://localhost:$sp" \
       PS4IPTV_NET_CA="$c/ca.crt" PS4IPTV_NET_CLOSED="http://127.0.0.1:$closed"
rc=0
echo "== plain"; "$OUT/http_it" || rc=1
echo "== ASan + UBSan"; ASAN_OPTIONS=detect_leaks=1 "$OUT/http_it_asan" || rc=1
exit $rc
