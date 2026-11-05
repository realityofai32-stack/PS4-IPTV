#!/usr/bin/env bash
# The host unit tests built with AddressSanitizer + UndefinedBehaviorSanitizer (any finding aborts the run), in
# build/host-tests-linux-asan. Usage: scripts/test-host-sanitize.sh
set -euo pipefail
export LC_ALL=C
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export HOST_TEST_VARIANT=asan
# -Wno-maybe-uninitialized: GCC reports false positives inside libstdc++ (std::regex) when instrumenting
export HOST_TEST_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -Wno-maybe-uninitialized"
# third-party stb_image_write (the tests' JPEG / PNG writer, never called by the app) shifts signed ints: that one
# check is off for stb_impl.cpp only; the stb_image decoder the app uses stays fully instrumented otherwise
export HOST_TEST_STB_FLAGS="-fno-sanitize=shift"
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
# the performance checks measure optimized code; under the sanitizers they only check results
export PS4IPTV_SANITIZED=1
"$repo/scripts/test-host.sh" "$@"
