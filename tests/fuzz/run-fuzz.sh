#!/usr/bin/env bash
# Coverage-guided fuzzing (libFuzzer, LLVM 12 from toolchain/) with UndefinedBehaviorSanitizer and libstdc++'s
# bounds assertions, then every corpus input replayed under GCC's AddressSanitizer + UBSan (LLVM 12's ASan runtime
# predates the host glibc's thread-local storage layout and cannot start here).
# Usage: tests/fuzz/run-fuzz.sh [seconds per target, default 60]
set -euo pipefail
export LC_ALL=C
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="$REPO/toolchain/llvm-12.0.1/bin/clang++"
OUT="$REPO/build/fuzz"
SECONDS_EACH="${1:-60}"
S="$REPO/src"
mkdir -p "$OUT"
FLAGS=(-std=c++17 -O1 -g -fsanitize=fuzzer,undefined -fno-sanitize-recover=all -D_GLIBCXX_ASSERTIONS -I "$S")
ASAN=(-std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
      -Wno-maybe-uninitialized -I "$S")
build() {
    local name="$1"; shift
    "$CXX" "${FLAGS[@]}" -o "$OUT/$name" "$REPO/tests/fuzz/$name.cpp" "$@"
    g++ "${ASAN[@]}" -o "$OUT/$name-asan" "$REPO/tests/fuzz/$name.cpp" "$REPO/tests/fuzz/replay_main.cpp" "$@"
}
build fuzz_stalker "$S/iptv/stalker.cpp" "$S/iptv/xtream.cpp" "$S/core/json.cpp" "$S/core/utf8.cpp" "$S/core/url.cpp" \
    "$S/core/hash.cpp" "$S/i18n/i18n.cpp" "$S/i18n/strings_en.cpp" "$S/i18n/strings_tr.cpp" "$S/platform/log.cpp" \
    "$S/platform/redact.cpp" "$S/platform/fs.cpp" "$S/platform/clock.cpp"
build fuzz_redact "$S/platform/redact.cpp"
build fuzz_keyboard "$S/platform/keyboard_logic.cpp" "$S/ui/keyboard_model.cpp" "$S/core/utf8.cpp" "$S/i18n/i18n.cpp" \
    "$S/platform/log.cpp" "$S/platform/redact.cpp" "$S/platform/fs.cpp" "$S/platform/clock.cpp" \
    "$S/i18n/strings_en.cpp" "$S/i18n/strings_tr.cpp"
build fuzz_paths "$S/media/local_media.cpp" "$S/platform/fs.cpp" "$S/player/protocols.cpp"
# seeds: the protocol's own shapes
seeds="$OUT/seeds-stalker"
mkdir -p "$seeds"
printf '%s' '{"js":{"token":"ABC","random":"x"}}' > "$seeds/token"
printf '%s' '{"js":{"total_items":3,"max_page_items":14,"data":[{"id":"1","name":"A","cmd":"ffmpeg http://h/1","tv_genre_id":"2","logo":"/l.png","series":[3,1,2],"is_series":"1","added":"2021-05-01 10:00:00","year":"2019-01-02","rating_imdb":"7.5"}]}}' > "$seeds/page"
printf '%s' '{"js":[{"id":"*","title":"All"},{"id":7,"title":"News"}]}' > "$seeds/cats"
printf '%s' '{"js":{"cmd":"ffmpeg http://s/play/live.php?mac=00:1A:79:00:00:01&play_token=Z","error":""}}' > "$seeds/link"
printf '%s' '{"js":{"data":[{"id":"9:1","name":"Season 1","series":[1,2],"cmd":"c"}]}}' > "$seeds/seasons"
printf '%s' 'http://portal.example.com:8080/stalker_portal/c/' > "$seeds/portal"
printf '%s' '00-1a-79-AB-cd-EF' > "$seeds/mac"
seedsr="$OUT/seeds-redact"; mkdir -p "$seedsr"
printf '%s' 'http://u:p@h.example.com/live/user/pass/1.ts?token=abc&e=1' > "$seedsr/a"
printf '%s' 'https://r.example.com/r?to=https%3A%2F%2Fh%2Fget.php%3Fusername%3Dx%26password%3Dy' > "$seedsr/b"
seedsp="$OUT/seeds-paths"; mkdir -p "$seedsp"
printf '%s' '/data/../mnt/usb0/./a//b.mkv' > "$seedsp/a"
printf '%s' 'https://user:pw@host:443/x' > "$seedsp/b"
rc=0
for t in fuzz_stalker:seeds-stalker fuzz_redact:seeds-redact fuzz_keyboard: fuzz_paths:seeds-paths; do
    name="${t%%:*}"; seed="${t#*:}"
    corpus="$OUT/corpus-$name"; mkdir -p "$corpus"
    args=("$corpus")
    [[ -n "$seed" ]] && args+=("$OUT/$seed")
    # inputs that once failed (tests/fuzz/regressions/<target>-*) are replayed every run
    short="${name#fuzz_}"
    reg="$OUT/regressions-$name"
    rm -rf "$reg"; mkdir -p "$reg"
    cp "$REPO/tests/fuzz/regressions/$short"-* "$reg/" 2>/dev/null || true
    args+=("$reg")
    log="$OUT/$name.log"
    if "$OUT/$name" "${args[@]}" -max_total_time="$SECONDS_EACH" -max_len=8192 -print_final_stats=1 \
        -artifact_prefix="$OUT/crash-$name-" > "$log" 2>&1 \
        && ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 "$OUT/$name-asan" "${args[@]}" > "$log.asan" 2>&1; then
        echo "ok    $name: $(grep -o 'stat::number_of_executed_units: [0-9]*' "$log" | awk '{print $2}') runs, \
$(grep -o 'cov: [0-9]*' "$log" | tail -1) edges, corpus $(ls "$corpus" | wc -l); ASan replay: $(tail -1 "$log.asan")"
    else
        echo "FAIL  $name (see $log)"; tail -30 "$log"; rc=1
    fi
done
exit $rc
