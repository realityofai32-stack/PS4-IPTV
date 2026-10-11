#!/usr/bin/env bash
# Release validation of the PS4 IPTV PKG on Linux (counterpart of scripts/verify-ps4iptv.ps1).
# Prints PASS / WARN / FAIL per check; exit code = number of FAILs. WARN = a check that could not be complete on
# this machine (e.g. no private secret list to compare against) - shown, never silently passed.
#
#   1  private config git-ignored and never committed
#   2  clean rebuild: zero errors, zero warnings from our sources (src/)
#   3  host unit tests
#   4  credential guard self-test
#   5  PkgTool pkg_validate
#   6  PKG contents: exact allow-list (no config, no credentials), fonts byte-identical
#   7  extracted PKG re-scanned for private secrets
#   8  linked system modules within the hardware-proven pPlay set
#   9  the text rendering test cannot open by itself
#   10 English + Turkish localization complete, no hardcoded screen text
#   11 download integration test (libcurl transport + manager, M3U service) against a local server
#   12 complete Git history scanned for private credentials
# Usage: scripts/verify-ps4iptv.sh [Release|Debug]
set -uo pipefail
export LC_ALL=C   # sort and comm must agree on the order

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
type="${1:-Release}"
tc="$repo/toolchain"
build="$repo/build/ps4iptv-${type,,}"
cmake="$tc/cmake/bin/cmake"
llvm="$tc/llvm-12.0.1/bin"
pkgtool() { LD_LIBRARY_PATH="$tc/openssl-1.1.1/lib" DOTNET_ROLL_FORWARD=Major DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 \
    "$tc/host-bin/PkgTool.Core" "$@"; }
export OPENORBIS="$tc/pacbrew/opt/pacbrew/ps4/openorbis" OO_PS4_TOOLCHAIN="$tc/pacbrew/opt/pacbrew/ps4/openorbis"
logs="$repo/build/verify-linux"
mkdir -p "$logs"
fails=0 warns=0 total=0
report() {   # PASS|WARN|FAIL name detail
    total=$((total + 1))
    [[ $1 == FAIL ]] && fails=$((fails + 1))
    [[ $1 == WARN ]] && warns=$((warns + 1))
    printf '%s  %s  %s\n' "$1" "$2" "$3"
}
x=""
cleanup() { [[ -n "$x" && -d "$x" ]] && rm -rf "$x"; }
trap cleanup EXIT
cd "$repo"

# 1
ignored=1
for f in config/test_streams.txt config/forbidden_strings.txt; do git check-ignore -q "$f" || ignored=0; done
tracked="$(git ls-files config/test_streams.txt config/forbidden_strings.txt)"
history="$(git log --all --oneline -- config/test_streams.txt config/forbidden_strings.txt)"
[[ $ignored == 1 && -z "$tracked" && -z "$history" ]] && r=PASS || r=FAIL
report $r '1 private config ignored, never committed' "ignored=$ignored tracked=${tracked:+yes} in-history=${history:+yes}"

# 2
scripts/build-ps4iptv.sh "$type" > "$logs/configure.log" 2>&1
"$cmake" --build "$build" --target clean > /dev/null 2>&1
"$cmake" --build "$build" --verbose > "$logs/build.log" 2>&1
buildrc=$?
errors=$(grep -cE ': error:|FAILED:|undefined symbol|CREDENTIAL GUARD' "$logs/build.log")
warn_all=$(grep -cE ':[0-9]+:[0-9]+: warning:' "$logs/build.log")
warn_ours=$(grep -E ':[0-9]+:[0-9]+: warning:' "$logs/build.log" | grep '/src/' | grep -vc pplay-reference)
[[ $buildrc == 0 && $errors == 0 && $warn_ours == 0 ]] && r=PASS || r=FAIL
report $r '2 clean build' "exit=$buildrc errors=$errors warnings(ours)=$warn_ours warnings(upstream)=$((warn_all - warn_ours))"
guardline="$(grep -m1 'credential guard:' "$logs/build.log" | sed 's/^-- //')"
[[ -n "$guardline" ]] && echo "      build $guardline"

# 3
scripts/test-host.sh > "$logs/host-tests.log" 2>&1
hostrc=$?
summary="$(grep -E '^[0-9]+ checks, ' "$logs/host-tests.log" | tr '\n' '|' | sed 's/|$//; s/|/ | /')"
[[ $hostrc == 0 && $(grep -cE '^[0-9]+ checks, 0 failures' "$logs/host-tests.log") -ge 2 ]] && r=PASS || r=FAIL
report $r '3 host unit tests' "$summary"

# 4
guard="$(scripts/test-credential-guard.sh 2>&1)"
[[ $? == 0 ]] && r=PASS || r=FAIL
report $r '4 credential guard self-test' "$(tail -1 <<< "$guard")"

# 5
pkg="$(ls -t "$build"/IV0001-IPTV00002_*.pkg 2>/dev/null | head -1)"
v="$(pkgtool pkg_validate "$pkg" 2>&1)"
okc=$(grep -c '^\[OK\]' <<< "$v")
bad=$(grep -E '^\[' <<< "$v" | grep -vc '^\[OK\]')
[[ $okc -gt 0 && $bad == 0 ]] && r=PASS || r=FAIL
report $r '5 PkgTool pkg_validate' "$(basename "$pkg"): $okc OK, $bad not OK"

# 6
x="$(mktemp -d "$repo/build/verify-extract-XXXXXX")"
pkgtool pkg_extract --passcode 00000000000000000000000000000000 "$pkg" "$x" > /dev/null 2>&1
uroot="$x/uroot"
files="$(cd "$uroot" && find . -type f | sed 's|^\./||' | sort)"
allowed="$(printf '%s\n' assets/cacert.pem assets/fonts/DejaVuSans.ttf assets/fonts/DroidSansFallback.ttf \
    assets/fonts/Inter-Regular.ttf assets/fonts/Inter-SemiBold.ttf eboot.bin sce_module/libSceFios2.prx \
    sce_module/libc.prx sce_sys/about/right.sprx sce_sys/keystone | sort)"
unexpected="$(comm -23 <(echo "$files") <(echo "$allowed") | tr '\n' ' ')"
missing="$(comm -13 <(echo "$files") <(echo "$allowed") | tr '\n' ' ')"
entries="$(pkgtool pkg_listentries "$pkg" 2>&1)"
sfo=0; grep -q PARAM_SFO <<< "$entries" && grep -q ICON0_PNG <<< "$entries" && sfo=1
fonts=1
for f in inter/Inter-Regular.ttf inter/Inter-SemiBold.ttf fonts-fallback/DejaVuSans.ttf fonts-fallback/DroidSansFallback.ttf; do
    cmp -s "$uroot/assets/fonts/$(basename "$f")" "third_party/$f" || fonts=0
done
[[ -z "$unexpected" && -z "$missing" && $sfo == 1 && $fonts == 1 ]] && r=PASS || r=FAIL
report $r '6 PKG contents allow-list' "fonts identical=$fonts sfo+icon=$sfo files=$(wc -l <<< "$files")${unexpected:+ | UNEXPECTED: $unexpected}${missing:+ | MISSING: $missing}"

# 7
scan="$("$cmake" "-DSCAN_DIR=$uroot" "-DSCAN_FILES=" "-DSECRET_SOURCES=$repo/config/test_streams.txt|$repo/config/forbidden_strings.txt" \
    -P cmake/scan_credentials.cmake 2>&1)"
scanrc=$?
detail="$(grep -o 'credential guard:.*' <<< "$scan" | tail -1)"
if [[ $scanrc != 0 ]]; then r=FAIL; elif grep -q 'against 0 private' <<< "$detail"; then r=WARN; detail="$detail (no private secret list on this machine: file-name rules only)"; else r=PASS; fi
report $r '7 extracted PKG secret scan' "$detail"

# 8
need="$("$llvm/llvm-objdump" -p "$build/ps4iptv" | awk '/NEEDED/ {print $2}' | sort)"
pplay="$repo/build/pplay-ps4-linux/pplay"
[[ -f "$pplay" ]] || pplay="$repo/build/pplay-ps4/pplay"
if [[ -f "$pplay" ]]; then
    proven="$("$llvm/llvm-objdump" -p "$pplay" | awk '/NEEDED/ {print $2}' | sort)"
    extra="$(comm -23 <(echo "$need") <(echo "$proven") | tr '\n' ' ')"
    [[ -z "$extra" ]] && r=PASS || r=FAIL
    report $r '8 system modules within the pPlay-proven set' "NEEDED: $(tr '\n' ' ' <<< "$need")${extra:+| NOT IN pPlay: $extra}"
else
    report WARN '8 system modules within the pPlay-proven set' "no pPlay reference ELF to compare against"
fi

# 9
notext=0; grep -q PS4IPTV_TEXT_TEST_AT_START "$build/build.ninja" || { grep -q 'PS4IPTV_TEXT_TEST_AT_START:BOOL=OFF' "$build/CMakeCache.txt" && notext=1; }
nodebug=1; [[ $type == Release ]] && grep -q PS4IPTV_DEBUG "$build/build.ninja" && nodebug=0
gated=0; grep -Pzo '#if defined\(PS4IPTV_DEBUG\) && defined\(PS4IPTV_TEXT_TEST_AT_START\)\s+push\(screens::makeTextTest\(\*this, true\)\);' src/app/app.cpp > /dev/null && gated=1
calls=$(grep -o 'makeTextTest(\*this, true)' src/app/app.cpp | wc -l)
[[ $notext == 1 && $nodebug == 1 && $gated == 1 && $calls == 1 ]] && r=PASS || r=FAIL
report $r '9 no automatic text rendering test' "launch option absent=$notext, debug defs absent=$nodebug, gated=$gated, launch calls=$calls"

# 10
i18n="$(grep -E '^(ok|FAIL) +i18n_' "$logs/host-tests.log")"
[[ $(grep -c '^ok' <<< "$i18n") -ge 5 && $(grep -c '^FAIL' <<< "$i18n") == 0 ]] && r=PASS || r=FAIL
report $r '10 localization (English + Turkish)' "$(tr -s ' ' <<< "$i18n" | tr '\n' '|' | sed 's/|$//')"

# 11
scripts/test-download-integration.sh > "$logs/download-it.log" 2>&1
itrc=$?
[[ $itrc == 0 ]] && r=PASS || r=FAIL
report $r '11 download integration test' "$(grep -E '^[0-9]+ checks, ' "$logs/download-it.log" | tr '\n' '|' | sed 's/|$//; s/|/ | /')"

# 12
hist="$(python3 -I scripts/scan-git-history.py "$repo" 2>&1)"
hrc=$?
if [[ $hrc == 0 ]]; then r=PASS; elif [[ $hrc == 2 ]] && grep -q 'RESULT: CLEAN' <<< "$hist"; then r=WARN; else r=FAIL; fi
report $r '12 Git history secret scan' "$(grep RESULT <<< "$hist")$( [[ $hrc == 2 ]] && echo ' (pattern scan only: no private secret list on this machine)')"
grep FINDING <<< "$hist" | sed 's/^/      /'

echo
echo "SUMMARY: $total checks, $fails failed, $warns warning(s)   (logs: build/verify-linux/)"
exit $fails
