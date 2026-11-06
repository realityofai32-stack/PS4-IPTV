#!/usr/bin/env bash
# Subtitle tests (GitHub issue #1, "arabic subtitles lag"): the PS4 subtitle stack's code (libass 0.14.0 without
# fontconfig / HarfBuzz, FriBidi 1.0.4, FreeType 2.10.1: build-host-ass-stack.sh) renders deterministic Arabic / RTL
# fixtures and their Latin controls frame by frame the way libmpv 0.34.1 does, with
#   - the font v0.3.0 shipped as subtitle font (Inter SemiBold): the reproduction (reported, not asserted to fail)
#   - the subtitle font this build installs (third_party/fonts-subtitles/V7Subtitles.ttf)
#   - the app's own log path (diag::write, redaction) and its log filter (player/mpv_log_filter)
# and checks: no glyph missing for Arabic / Persian / Hebrew / mixed text, render cost within 2x of the Latin
# control, no timing difference (an event is drawn exactly while it is due), a bounded number of log lines, no
# crash / hang on malformed, invalid UTF-8, huge and long inputs (also under ASan + UBSan), and embedded tracks
# (MKV SubRip / ASS, MP4 mov_text) decoded by the console's FFmpeg 5.0 with text and timing identical to the source.
# Results: build/subtitle-tests/summary.txt and results.tsv. Usage: tests/subtitles/run-subtitle-tests.sh
set -uo pipefail
export LC_ALL=C
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="$REPO/tests/subtitles"
OUT="$REPO/build/subtitle-tests"
mkdir -p "$OUT"
SUM="$OUT/summary.txt"
TSV="$OUT/results.tsv"
: > "$SUM"
fails=0
pass() { echo "ok    $*" | tee -a "$SUM"; }
fail() { echo "FAIL  $*" | tee -a "$SUM"; fails=$((fails + 1)); }

"$T/build-host-ass-stack.sh" > "$OUT/stack.log" 2>&1 || { tail "$OUT/stack.log"; echo "stack build failed"; exit 1; }
"$T/build-bench.sh" "$OUT/ass_bench" || exit 1
BENCH_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" \
    "$T/build-bench.sh" "$OUT/ass_bench_asan" || exit 1
rm -rf "$OUT/fixtures"
python3 -I "$T/make_fixtures.py" "$OUT/fixtures" --stress || exit 1
FX="$OUT/fixtures"

# mpv's config dir as the app prepares it: <config-dir>/subfont.ttf
mkdir -p "$OUT/font-v030" "$OUT/font-new"
cp "$REPO/third_party/inter/Inter-SemiBold.ttf" "$OUT/font-v030/subfont.ttf"
cp "$REPO/third_party/fonts-subtitles/V7Subtitles.ttf" "$OUT/font-new/subfont.ttf"

val() { awk -v k="$1" '$1==k{print $2}' "$2"; }
run() {   # run NAME FONT FILE [extra args]: 30 fps over the whole file (at most 300 s)
    local name="$1" font="$2" file="$3"; shift 3
    local r="$OUT/run-$name-$font.txt"
    "$OUT/ass_bench" --sub "$file" --subfont "$OUT/font-$font/subfont.ttf" --fps 30 --to 300 --timing "$@" > "$r" 2>&1
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$font" "$(val render_mean_ms "$r")" "$(val render_p99_ms "$r")" \
        "$(val app_lines_logged "$r")" "$(val glyph_missing_in_font "$r")" "$(val glyph_select_more "$r")" \
        "$(val timing_mismatch "$r")" >> "$TSV"
    echo "$r"
}
printf 'fixture\tfont\trender_mean_ms\trender_p99_ms\tapp_log_lines\tglyph_missing\tglyph_searches\ttiming_mismatch\n' > "$TSV"

# 1. RTL fixtures against their Latin controls
pairs="ar_single:en_single ar_multi:en_multi ar_rlm:en_rlm_control ar_mixed:en_mixed ar_diacritics:en_diacritics
ar_fast:en_fast ar_long:en_long fa_ur:en_single he:en_single mixed_rtl_ltr:en_mixed ar_styled.ass:en_styled.ass"
for p in $pairs; do
    rtl="${p%%:*}"; ltr="${p##*:}"
    frtl="$FX/$rtl"; fltr="$FX/$ltr"
    [[ "$rtl" == *.ass ]] || frtl="$frtl.srt"
    [[ "$ltr" == *.ass ]] || fltr="$fltr.srt"
    old="$(run "$rtl" v030 "$frtl")"
    new="$(run "$rtl" new "$frtl")"
    ctl="$(run "$ltr" new "$fltr")"
    nm="$(val render_mean_ms "$new")"; cm="$(val render_mean_ms "$ctl")"; om="$(val render_mean_ms "$old")"
    miss="$(val glyph_missing_in_font "$new")"; srch="$(val glyph_select_more "$new")"
    lines="$(val app_lines_logged "$new")"; tm="$(val timing_mismatch "$new")"
    allowed=0
    [[ "$rtl" == fa_ur ]] && allowed=1   # U+06C1 / U+06D2 (Urdu): in no bundled font (third_party/fonts-subtitles)
    detail="v0.3.0 font: $(val app_lines_logged "$old") log lines, $(val glyph_select_more "$old") glyph searches, ${om} ms/frame | new: ${nm} ms/frame (Latin control ${cm}), $lines log lines, $srch searches"
    if [[ "$tm" != 0 ]]; then
        fail "$rtl: $tm frame(s) with subtitles drawn while not due or missing while due"
    elif (( allowed == 0 )) && [[ "$miss" != 0 || "$srch" != 0 ]]; then
        fail "$rtl: $srch glyph search(es) with the subtitle font ($detail)"
    elif awk -v n="$nm" -v c="$cm" 'BEGIN{exit !(n > 2*c + 0.02)}'; then
        fail "$rtl: render cost $nm ms/frame vs Latin control $cm ($detail)"
    elif (( allowed == 0 )) && (( lines > 10 )); then
        fail "$rtl: $lines log lines ($detail)"
    else
        pass "$rtl: $detail"
    fi
done

# 2. scripts the subtitle font does not have (CJK, emoji) and the Urdu letters: the log filter keeps the log quiet.
# Bound: the first FONT_LINES_KEPT (6) lines, one summary per 30 s (300 s: 10), the final summary, one spare.
MAXLINES=18
for f in missing_glyphs fa_ur; do
    r="$(run "$f-filtered" new "$FX/$f.srt" --filter)"
    lines="$(val app_lines_logged "$r")"
    if (( lines <= MAXLINES )) && grep -q '^filter_summary subtitles: the subtitle font has no glyph' "$r"; then
        pass "$f with the log filter: $lines log line(s) (unfiltered: $(val app_lines_logged "$OUT/run-$f-new.txt" 2>/dev/null || echo ?)); $(grep '^filter_summary' "$r" | cut -c16-120)"
    else
        fail "$f with the log filter: $lines log line(s)"
    fi
done
r="$(run ar_single-v030-filtered v030 "$FX/ar_single.srt" --filter)"
lines="$(val app_lines_logged "$r")"
(( lines <= MAXLINES )) && pass "the log filter alone (v0.3.0 font, Arabic): $lines log line(s) instead of $(val app_lines_logged "$OUT/run-ar_single-v030.txt")" \
    || fail "the log filter alone (v0.3.0 font): $lines log lines"

# 3. Turkish / accented Latin: unchanged font, no searches
for f in tr accented; do
    r="$(run "$f" new "$FX/$f.srt")"
    [[ "$(val glyph_select_more "$r")" == 0 && "$(val timing_mismatch "$r")" == 0 ]] \
        && pass "$f: no glyph search, $(val render_mean_ms "$r") ms/frame" || fail "$f: glyph searches or timing"
done

# 4. malformed / invalid / huge inputs: complete (no crash, no hang), plain and under ASan + UBSan
for f in malformed.srt invalid_utf8.srt bom_crlf.srt stress_long_line.srt stress_100k_events.srt ar_styled.ass \
         ar_fast.srt missing_glyphs.srt; do
    for b in ass_bench ass_bench_asan; do
        t0=$(date +%s.%N)
        if timeout 600 "$OUT/$b" --sub "$FX/$f" --subfont "$OUT/font-new/subfont.ttf" --fps 30 --to 20 --filter \
            > "$OUT/robust-$b-$f.txt" 2>&1; then
            t1=$(date +%s.%N)
            pass "$f ($b): $(val events "$OUT/robust-$b-$f.txt") events, $(val srt_malformed "$OUT/robust-$b-$f.txt") malformed block(s) skipped, max frame $(val render_max_ms "$OUT/robust-$b-$f.txt") ms, $(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.1f", b-a}') s"
        else
            fail "$f ($b): exit $? ($(tail -2 "$OUT/robust-$b-$f.txt" | tr '\n' ' '))"
        fi
    done
done

# 5. embedded tracks through the console's FFmpeg 5.0 (host build of the PS4 configuration)
FF="$REPO/build/host-tls/ffmpeg"
if [[ -f "$FF/lib/libavformat.a" ]] && command -v ffmpeg > /dev/null; then
    gcc -O2 -Wall -Wextra -Werror -I "$FF/include" "$T/embedded_dump.c" -o "$OUT/embedded_dump" \
        "$FF/lib/libavformat.a" "$FF/lib/libavcodec.a" "$FF/lib/libswresample.a" "$FF/lib/libavutil.a" \
        -L "$REPO/build/host-tls/mbedtls/lib" -lmbedtls -lmbedx509 -lmbedcrypto -lm -pthread || exit 1
    for spec in "ar_rlm.srt:mkv:srt" "ar_rlm.srt:mp4:mov_text" "ar_mixed.srt:mkv:srt" "ar_diacritics.srt:mp4:mov_text" \
                "ar_styled.ass:mkv:ass" "he.srt:mkv:srt"; do
        IFS=: read -r src ext codec <<< "$spec"
        media="$OUT/embedded-${src%.*}-$codec.$ext"
        # the media files are made by the system FFmpeg (the host build has no encoders / muxers)
        ffmpeg -v error -y -f lavfi -i color=c=black:s=320x240:d=40 -i "$FX/$src" -map 0 -map 1 -c:v libx264 \
            -preset ultrafast -c:s "$codec" -metadata:s:s:0 language=ara -t 40 "$media" || { fail "mux $spec"; continue; }
        "$OUT/embedded_dump" "$media" > "$media.events" 2> "$media.log" || { fail "decode $spec"; continue; }
        if python3 -I "$T/compare_embedded.py" "$FX/$src" "$media.events" 40000 "$media.srt" > "$media.cmp"; then
            r="$OUT/run-embedded-${src%.*}-$codec.txt"
            "$OUT/ass_bench" --sub "$media.srt" --subfont "$OUT/font-new/subfont.ttf" --fps 30 --timing > "$r" 2>&1
            if [[ "$(val glyph_select_more "$r")" == 0 && "$(val timing_mismatch "$r")" == 0 ]]; then
                pass "embedded $src in .$ext ($codec, decoder $(grep -o 'codec .*' "$media.log")): $(cat "$media.cmp"); rendered with no glyph search, $(val render_mean_ms "$r") ms/frame"
            else
                fail "embedded $src in .$ext: glyph searches $(val glyph_select_more "$r"), timing $(val timing_mismatch "$r")"
            fi
        else
            fail "embedded $src in .$ext: $(tr '\n' ' ' < "$media.cmp")"
        fi
    done
else
    echo "SKIP  embedded tracks: build/host-tls (tests/https/build-host-tls-stack.sh) or ffmpeg missing" | tee -a "$SUM"
fi

# 6. the font file is the generator's output (reproducible) when fontTools is available
if [[ -d "$REPO/toolchain/fonttools/fontTools" ]]; then
    python3 -I "$REPO/scripts/make-subtitle-font.py" --check > "$OUT/font-check.txt" 2>&1 \
        && pass "$(cat "$OUT/font-check.txt")" || fail "$(cat "$OUT/font-check.txt")"
fi

echo "$(grep -c '^ok' "$SUM") passed, $fails failure(s)" | tee -a "$SUM"
(( fails == 0 ))
