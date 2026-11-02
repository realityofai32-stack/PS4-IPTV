// The UI clock (clockx::monotonic) must follow wall time, also while the app idles. On the console the old
// clock (libc++ steady_clock -> clock_gettime(1) = FreeBSD CLOCK_VIRTUAL, user CPU time) hardly moved while the
// main loop slept between frames, so a held d-pad direction waited seconds for its first repeat (v7-ui-2 Live TV).

#include <chrono>
#include <cstdio>
#include <thread>

#include "check.h"
#include "../../src/platform/clock.h"
#include "../../src/platform/input_logic.h"

using namespace input;

namespace {
    // the app's idle frame: no drawing, an 8 ms sleep (App::run IDLE_SLEEP_MS)
    void idleFrame() {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

TEST(clock_monotonic_follows_wall_time_while_idle) {
    double t0 = clockx::monotonic();
    auto w0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));   // no CPU used at all
    double dt = clockx::monotonic() - t0;
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
    CHECK(dt >= 0.14);                       // a CPU-time clock would report ~0 here
    CHECK(dt <= wall + 0.01);
    double a = clockx::monotonic();
    double b = clockx::monotonic();
    CHECK(b >= a);                           // never goes back
    CHECK(!clockx::clockReport().empty());
}

// the hold path end to end on the real UI clock: an idle loop of 8 ms sleeps, the d-pad held for 1.2 s
TEST(hold_repeat_runs_on_the_ui_clock_while_idle) {
    KeyRepeater k;
    int presses = 0, repeats = 0;
    double first = -1;
    const double start = clockx::monotonic();
    for (double t = start; t - start < 1.2; t = clockx::monotonic()) {
        KeyEvent e = k.update(true, t, true);
        if (e == KeyEvent::Press) {
            presses++;
        } else if (e == KeyEvent::Repeat) {
            repeats++;
            if (first < 0) {
                first = t - start;
            }
        }
        idleFrame();
    }
    // 0.30 s delay, then every 90 ms (0.3..1.0 s: ~7) and 60 ms (1.0..1.2 s: ~3); sleeps overshoot on a busy
    // host, so the bounds are loose - a CPU-time clock gives no repeat at all
    std::printf("     1.2 s hold on an idle loop: first repeat at %.3f s, %d repeats\n", first, repeats);
    CHECK_EQ(presses, 1);
    CHECK(first >= 0.29 && first < 0.45);
    CHECK(repeats >= 6 && repeats <= 14);
    // release stops at once
    CHECK(k.update(false, clockx::monotonic(), true) == KeyEvent::None);
    CHECK(k.update(false, clockx::monotonic() + 1, true) == KeyEvent::None);
}

// why the clock matters: the same loop on a clock that only advances with CPU work (~0.2 ms per idle frame)
TEST(hold_repeat_never_starts_on_a_cpu_time_clock) {
    KeyRepeater k;
    int repeats = 0;
    double cpu = 0;
    for (int frame = 0; frame < 150; frame++) {   // 150 idle frames = 1.2 s of wall time
        repeats += k.update(true, cpu, true) == KeyEvent::Repeat;
        cpu += 0.0002;
    }
    CHECK_EQ(repeats, 0);   // the v7-ui-2 hardware symptom: one step per press, no hold-to-scroll
}
