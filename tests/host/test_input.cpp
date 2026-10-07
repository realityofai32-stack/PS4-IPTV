// DS4 navigation: key repeat timing/acceleration, analog stick filter, list paging and scrollbar math.

#include "check.h"
#include "../../src/platform/input_logic.h"
#include "../../src/ui/scroll_math.h"

using namespace input;

namespace {
    // drives one repeater at a fixed frame period and records event times
    std::vector<double> holdFor(KeyRepeater &k, double seconds, double frame, bool repeatable = true) {
        std::vector<double> events;
        for (double t = 0; t <= seconds + 1e-9; t += frame) {
            KeyEvent e = k.update(true, t, repeatable);
            if (e != KeyEvent::None) {
                events.push_back(t);
            }
        }
        return events;
    }

    bool near(double a, double b, double tol) {
        return a > b - tol && a < b + tol;
    }
}

TEST(repeat_first_press_is_immediate_then_initial_delay) {
    RepeatTiming t;
    KeyRepeater k(t);
    CHECK(k.update(true, 10.0, true) == KeyEvent::Press);
    CHECK(k.update(true, 10.0 + t.initialDelay - 0.01, true) == KeyEvent::None);
    CHECK(k.update(true, 10.0 + t.initialDelay, true) == KeyEvent::Repeat);
    CHECK_EQ(k.repeatCount(), 1);
    // the next repeat follows after `interval`
    CHECK(k.update(true, 10.0 + t.initialDelay + t.interval - 0.01, true) == KeyEvent::None);
    CHECK(k.update(true, 10.0 + t.initialDelay + t.interval + 0.001, true) == KeyEvent::Repeat);
    // timings stay inside the requested ranges
    CHECK(t.initialDelay >= 0.30 && t.initialDelay <= 0.40);
    CHECK(t.interval >= 0.090 && t.interval <= 0.120);
    CHECK(t.turboInterval >= 0.050 && t.turboInterval <= 0.070);
}

TEST(repeat_cadence_and_acceleration_at_60fps) {
    RepeatTiming t;
    KeyRepeater k(t);
    std::vector<double> ev = holdFor(k, 6.0, 1.0 / 60);
    CHECK(ev.size() > 60);
    CHECK(near(ev[0], 0, 1e-9));
    CHECK(near(ev[1], t.initialDelay, 1.0 / 60 + 1e-6));
    // gaps: ~interval at first, ~fastInterval after fastAfter, ~turboInterval after turboAfter
    auto gapAround = [&](double when) {
        for (size_t i = 1; i < ev.size(); i++) {
            if (ev[i] >= when) {
                return ev[i] - ev[i - 1];
            }
        }
        return -1.0;
    };
    CHECK(near(gapAround(0.8), t.interval, 1.0 / 60 + 1e-6));
    CHECK(near(gapAround(2.5), t.fastInterval, 1.0 / 60 + 1e-6));
    CHECK(near(gapAround(5.0), t.turboInterval, 1.0 / 60 + 1e-6));
    // average rate in the turbo phase matches the interval (no drift from frame rounding)
    int turbo = 0;
    for (double e: ev) {
        turbo += e >= 4.0 && e < 6.0;
    }
    CHECK(turbo >= (int) (2.0 / t.turboInterval) - 1 && turbo <= (int) (2.0 / t.turboInterval) + 1);
    CHECK(near(repeatInterval(t, 0.5), t.interval, 1e-9));
    CHECK(near(repeatInterval(t, 2.0), t.fastInterval, 1e-9));
    CHECK(near(repeatInterval(t, 9.0), t.turboInterval, 1e-9));
}

TEST(repeat_no_burst_after_long_frame_and_release_resets) {
    RepeatTiming t;
    KeyRepeater k(t);
    CHECK(k.update(true, 0, true) == KeyEvent::Press);
    CHECK(k.update(true, 1.0, true) == KeyEvent::Repeat);
    // a 2-second hitch: exactly one event, then the normal cadence resumes from now
    CHECK(k.update(true, 3.0, true) == KeyEvent::Repeat);
    CHECK(k.update(true, 3.001, true) == KeyEvent::None);
    CHECK(k.update(true, 3.0 + t.fastInterval + 0.001, true) == KeyEvent::Repeat);
    // release: no events, and the next press is a fresh press with the full initial delay
    CHECK(k.update(false, 3.2, true) == KeyEvent::None);
    CHECK(!k.isDown());
    CHECK(k.update(true, 3.3, true) == KeyEvent::Press);
    CHECK(k.update(true, 3.3 + t.initialDelay - 0.02, true) == KeyEvent::None);
    // non-repeatable buttons (Cross, Circle...) only ever press once
    KeyRepeater c(t);
    CHECK_EQ(holdFor(c, 5.0, 1.0 / 60, false).size(), (size_t) 1);
}

TEST(stick_deadzone_hysteresis_and_direction_changes) {
    StickFilter s;
    // drift and small movements around the centre never navigate
    CHECK(s.update(3000, -4000) == StickDir::None);
    CHECK(s.update(0, StickFilter::ENGAGE - 1) == StickDir::None);
    // engage
    CHECK(s.update(0, 20000) == StickDir::Down);
    // hysteresis: wobbling between RELEASE and ENGAGE keeps the direction (no repeated presses)
    for (int v: {15000, 12000, 16500, 10500, 14000}) {
        CHECK(s.update(0, v) == StickDir::Down);
    }
    // near the centre: released
    CHECK(s.update(0, StickFilter::RELEASE - 1) == StickDir::None);
    // straight to the opposite side: immediate change
    CHECK(s.update(0, 25000) == StickDir::Down);
    CHECK(s.update(0, -25000) == StickDir::Up);
    // diagonal: the dominant axis wins, a weaker perpendicular push does not steal the direction
    CHECK(s.update(9000, -24000) == StickDir::Up);
    CHECK(s.update(17000, -24000) == StickDir::Up);
    CHECK(s.update(26000, -18000) == StickDir::Right);
    CHECK(s.update(-32768, 0) == StickDir::Left);
    CHECK(s.update(0, 0) == StickDir::None);
}

TEST(stick_threshold_jitter_does_not_oscillate) {
    StickFilter s;
    KeyRepeater down;
    int presses = 0;
    double t = 0;
    // the stick rests right at the engage threshold with sensor noise for two seconds
    for (int i = 0; i < 120; i++, t += 1.0 / 60) {
        int y = StickFilter::ENGAGE + ((i % 2) ? 400 : -400);
        bool isDown = s.update(0, y) == StickDir::Down;
        presses += down.update(isDown, t, true) == KeyEvent::Press;
    }
    CHECK_EQ(presses, 1);
    // returning to the centre resets the repeat timer: the next push starts with a fresh press
    CHECK(down.update(s.update(0, 0) == StickDir::Down, t, true) == KeyEvent::None);
    CHECK(down.update(s.update(0, 30000) == StickDir::Down, t + 0.05, true) == KeyEvent::Press);
}

TEST(page_jump_and_first_visible) {
    CHECK_EQ(scroll::pageTarget(0, 1, 8, 573), 8);
    CHECK_EQ(scroll::pageTarget(570, 1, 8, 573), 572);
    CHECK_EQ(scroll::pageTarget(5, -1, 8, 573), 0);
    CHECK_EQ(scroll::pageTarget(100, -2, 8, 573), 84);
    CHECK_EQ(scroll::pageTarget(3, 1, 0, 10), 4);     // page size is at least one row
    CHECK_EQ(scroll::pageTarget(0, 1, 8, 0), 0);
    // keeps one row of context, never scrolls past the ends
    CHECK_EQ(scroll::firstVisible(0, 0, 9, 573, 1), 0);
    CHECK_EQ(scroll::firstVisible(8, 0, 9, 573, 1), 1);
    CHECK_EQ(scroll::firstVisible(7, 0, 9, 573, 1), 0);
    CHECK_EQ(scroll::firstVisible(572, 0, 9, 573, 1), 564);
    CHECK_EQ(scroll::firstVisible(100, 200, 9, 573, 1), 99);
    CHECK_EQ(scroll::firstVisible(3, 0, 9, 5, 1), 0);  // fits: never scrolls
}

TEST(scrollbar_thumb_math) {
    scroll::Thumb none = scroll::thumb(760, 8, 9, 0, 40);
    CHECK(!none.visible);
    CHECK(!scroll::thumb(760, 9, 9, 0, 40).visible);
    scroll::Thumb top = scroll::thumb(760, 573, 9, 0, 40);
    CHECK(top.visible);
    CHECK_EQ(top.offset, 0.0f);
    CHECK_EQ(top.length, 40.0f);   // 760 * 9 / 573 = 12: raised to the minimum
    scroll::Thumb bottom = scroll::thumb(760, 573, 9, 564, 40);
    CHECK(bottom.offset + bottom.length > 759.9f && bottom.offset + bottom.length < 760.1f);
    scroll::Thumb half = scroll::thumb(800, 20, 10, 5, 10);
    CHECK_EQ(half.length, 400.0f);  // proportional
    CHECK_EQ(half.offset, 200.0f);  // first 5 of max 10
    scroll::Thumb clamped = scroll::thumb(800, 20, 10, 99, 10);
    CHECK_EQ(clamped.offset, 400.0f);
}
