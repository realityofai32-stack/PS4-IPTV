#!/usr/bin/env bash
# The PS4 APP_VER derivation (cmake/app_version.cmake) and the single version source (CMakeLists.txt APP_VERSION).
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CMAKE="$REPO/toolchain/cmake/bin/cmake"
[[ -x "$CMAKE" ]] || CMAKE=cmake
fails=0
derive() {
    local f; f="$(mktemp)"
    printf 'include("%s/cmake/app_version.cmake")\napp_pkg_version("%s" v)\nmessage(STATUS "PKG=${v}")\n' "$REPO" "$1" > "$f"
    "$CMAKE" -P "$f" 2>&1 | sed -n 's/^-- PKG=//p'
    rm -f "$f"
}
for pair in 0.3.0=00.30 0.4.0=00.40 0.4.1=00.41 0.9.9=00.99 1.0.0=01.00 1.2.3=01.23 12.0.5=12.05 99.9.9=99.99; do
    got="$(derive "${pair%%=*}")"
    [[ "$got" == "${pair##*=}" ]] && echo "ok    ${pair%%=*} -> $got" || { echo "FAIL  ${pair%%=*} -> '$got' (want ${pair##*=})"; fails=$((fails+1)); }
done
for bad in 0.10.0 0.3 3.0 v0.3.0 0.3.0.1 100.0.0 00.3.0 0.3.0-rc1 "" 0.3.10; do
    got="$(derive "$bad")"
    [[ -z "$got" ]] && echo "ok    '$bad' refused" || { echo "FAIL  '$bad' -> '$got' (must be refused)"; fails=$((fails+1)); }
done
# one place: the version literal appears in CMakeLists.txt only (the playback test app has its own)
n=$(grep -rn --include=*.cpp --include=*.h --include=*.in -E '"0\.[0-9]\.[0-9]"' "$REPO/src" | wc -l)
[[ $n == 0 ]] && echo "ok    no version literal in src/" || { echo "FAIL  version literal in src/"; fails=$((fails+1)); }
echo "$fails failure(s)"
exit $fails
