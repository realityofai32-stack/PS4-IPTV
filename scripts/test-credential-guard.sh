#!/usr/bin/env bash
# Self-test of cmake/scan_credentials.cmake (Linux counterpart of scripts/test-credential-guard.ps1): it must pass
# on clean input and fail on planted secrets. Planted secrets come from config/test_streams.txt when it exists;
# otherwise a clearly synthetic Xtream URL is generated (this machine has no private config). They are written
# only into a temporary folder that is deleted afterwards; nothing secret is printed.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmake="$repo/toolchain/cmake/bin/cmake"
script="$repo/cmake/scan_credentials.cmake"
tmp="$(mktemp -d "$repo/build/guard-test-XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
cfg="$repo/config/test_streams.txt"
note=""
if [[ ! -f "$cfg" ]]; then
    cfg="$tmp/test_streams.txt"
    printf 'TS=http://guard-test.example.com:8080/live/synthuser%s/synthpass%s/123.ts\n' "$RANDOM" "$RANDOM" > "$cfg"
    note=" (synthetic secrets: no config/test_streams.txt on this machine)"
fi
sources="$cfg|$repo/config/forbidden_strings.txt"
fail=0

guard() { "$cmake" "-DSCAN_DIR=$1" "-DSCAN_FILES=$2" "-DSECRET_SOURCES=${3:-$sources}" -P "$script" >/dev/null 2>&1; echo $?; }
expect() {   # name, should pass (1/0), exit code
    local ok=0
    if { [[ $2 == 1 ]] && [[ $3 == 0 ]]; } || { [[ $2 == 0 ]] && [[ $3 != 0 ]]; }; then ok=1; fi
    (( ok )) || fail=$((fail + 1))
    printf '%s  %s (exit %s, expected %s)\n' "$( ((ok)) && echo PASS || echo FAIL)" "$1" "$3" "$( [[ $2 == 1 ]] && echo success || echo failure)"
}

password="$(grep -m1 -oE '/live/[^/]+/[^/]+/' "$cfg" | cut -d/ -f4)"
[[ -n "$password" ]] || { echo "no Xtream URL in $cfg"; exit 1; }
mkdir -p "$tmp/clean" "$tmp/name" "$tmp/content" "$tmp/playlist" "$tmp/playlistcache"
printf 'nothing to see' > "$tmp/clean/readme.txt"
expect 'clean folder passes' 1 "$(guard "$tmp/clean" '')"
printf 'TS=<placeholder>' > "$tmp/name/test_streams.txt"
expect 'file named test_streams.txt is rejected' 0 "$(guard "$tmp/name" '')"
printf '#EXTM3U\n#EXTINF:-1,A\nhttp://stream.example.com/a.ts' > "$tmp/playlist/channels.M3U8"
expect 'an M3U / M3U8 playlist is rejected' 0 "$(guard "$tmp/playlist" '')"
printf '#EXTM3U' > "$tmp/playlistcache/m3u_playlist.json"
expect 'a saved playlist copy is rejected' 0 "$(guard "$tmp/playlistcache" '')"
printf '\x00\x01\x02x%s\x00' "$password" > "$tmp/content/blob.bin"
expect 'binary containing the password is rejected' 0 "$(guard "$tmp/clean" "$tmp/content/blob.bin")"
printf 'this file has no key=value secrets in a known form?' > "$tmp/unparsable.txt"
expect 'secret source without extractable secrets fails closed' 0 "$(guard "$tmp/clean" '' "$tmp/unparsable.txt")"
echo "credential guard self-test: $fail failure(s)$note"
exit $fail
