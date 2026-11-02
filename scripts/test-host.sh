#!/usr/bin/env bash
# Builds and runs the host-side unit tests natively on Linux with g++ (-Wall -Wextra -Werror): the
# Linux counterpart of scripts/run-host-tests.ps1 (MSVC /W4 /WX). Same sources, same test binaries:
#   - app_tests:    production app (core, Xtream, M3U, storage, input, images, playback, downloads, i18n, ...)
#   - host_tests:   playback test app (redaction, test_streams.txt parsing)
#   - config_check: playback test app config checker
# Usage: scripts/test-host.sh [--no-run]
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$repo/build/host-tests-linux"
mkdir -p "$out"

cxx="${CXX:-g++}"
# -O2: the performance tests (search index, sorting, parsing at real catalog scale) measure optimized code
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -pthread
       # stb_image.h / stb_image_write.h: the copies libcross2d compiles into the PS4 build
       -I "$repo/external/pplay-reference/libcross2d/include")

build() {
    local name="$1"; shift
    local obj="$out/obj-$name"
    mkdir -p "$obj"
    local objs=() pids=() src o
    # compile in parallel, one object per source (incremental: unchanged sources are not rebuilt)
    for src in "$@"; do
        o="$obj/$(echo "${src#$repo/}" | tr '/' '_').o"
        objs+=("$o")
        if [[ ! -f "$o" || "$src" -nt "$o" || -n "$(find "$repo/src" "$repo/tests" -name '*.h' -newer "$o" -print -quit)" ]]; then
            local extra=()
            # third-party stb_image (MSVC: #pragma warning(push, 0) in stb_impl.cpp)
            [[ "$src" == */stb_impl.cpp ]] && extra=(-w)
            "$cxx" "${flags[@]}" "${extra[@]}" -I "$repo/src" -c "$src" -o "$o" &
            pids+=($!)
            if (( ${#pids[@]} >= $(nproc) )); then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
        fi
    done
    local failed=0
    for pid in "${pids[@]}"; do wait "$pid" || failed=1; done
    (( failed == 0 )) || { echo "$name: compile failed" >&2; exit 1; }
    "$cxx" "${flags[@]}" -o "$out/$name" "${objs[@]}"
}

s="$repo/src"
h="$repo/tests/host"
build app_tests \
    "$h/test_main.cpp" "$h/test_core.cpp" "$h/test_iptv.cpp" "$h/test_storage.cpp" "$h/test_jobs.cpp" \
    "$h/test_live.cpp" "$h/test_text.cpp" "$h/test_input.cpp" "$h/test_images.cpp" "$h/test_playback.cpp" \
    "$h/test_vod.cpp" "$h/test_tracks.cpp" "$h/test_search.cpp" "$h/test_progress.cpp" "$h/test_catalog.cpp" \
    "$h/test_downloads.cpp" "$h/test_i18n.cpp" "$h/test_display.cpp" "$h/test_m3u.cpp" "$h/test_theme.cpp" \
    "$h/test_layout.cpp" "$h/test_image_fit.cpp" "$h/test_clock.cpp" \
    "$h/stb_impl.cpp" \
    "$s/core/utf8.cpp" "$s/core/json.cpp" "$s/core/url.cpp" "$s/iptv/xtream.cpp" "$s/iptv/catalog.cpp" \
    "$s/iptv/m3u.cpp" \
    "$s/iptv/search_index.cpp" "$s/app/vod_progress.cpp" "$s/app/series_plan.cpp" \
    "$s/storage/library_store.cpp" "$s/storage/catalog_cache.cpp" \
    "$s/platform/fs.cpp" "$s/platform/redact.cpp" "$s/platform/log.cpp" "$s/network/jobs.cpp" \
    "$s/storage/profile_store.cpp" "$s/storage/settings_store.cpp" "$s/ui/keyboard_model.cpp" \
    "$s/platform/input_logic.cpp" "$s/ui/scroll_math.cpp" "$s/ui/image_fit.cpp" "$s/platform/clock.cpp" \
    "$s/player/stability.cpp" \
    "$s/images/image_key.cpp" "$s/images/disk_cache.cpp" "$s/images/image_decode.cpp" \
    "$s/images/image_scheduler.cpp" "$s/images/image_pipeline.cpp" "$s/core/format.cpp" \
    "$s/player/tracks.cpp" "$s/player/display.cpp" "$s/i18n/i18n.cpp" "$s/i18n/strings_en.cpp" \
    "$s/i18n/strings_tr.cpp" "$s/downloads/download_model.cpp" "$s/downloads/part_file.cpp" \
    "$s/downloads/download_manager.cpp" "$s/app/offline.cpp"

t="$repo/tests/playback-test"
build host_tests "$t/host-tests/host_tests.cpp" "$t/src/redact.cpp" "$t/src/stream_config.cpp"
build config_check "$t/host-tests/config_check.cpp" "$t/src/redact.cpp" "$t/src/stream_config.cpp"

[[ "${1:-}" == "--no-run" ]] && exit 0

samples="$repo/build/xtream-samples"   # optional, from scripts/fetch-xtream-samples.py
if [[ -d "$samples" ]]; then export PS4IPTV_SAMPLES="$samples"; else unset PS4IPTV_SAMPLES; fi
icons="$repo/build/icon-samples"       # optional: real provider logos (never committed)
if [[ -d "$icons" ]]; then export PS4IPTV_ICON_SAMPLES="$icons"; else unset PS4IPTV_ICON_SAMPLES; fi
export PS4IPTV_SOURCE_DIR="$s"         # localization checks scan the sources
export PS4IPTV_TEST_TMP="$out/tmp"
mkdir -p "$PS4IPTV_TEST_TMP"

rc=0
"$out/app_tests" || rc=1
"$out/host_tests" || rc=1
if (( rc != 0 )); then echo "host tests failed" >&2; fi
exit $rc
