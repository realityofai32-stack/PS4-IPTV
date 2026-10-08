// DS4 navigation: key repeat timing / acceleration curve, analog stick filter and full-deflection speed, hold
// navigation on catalog-sized lists, viewport easing, list paging and scrollbar math.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "check.h"
#include "../../src/platform/input_logic.h"
#include "../../src/ui/scroll_math.h"

using namespace input;

namespace {
    // drives one repeater at a fixed frame period and records event times
    std::vector<double> holdFor(KeyRepeater &k, double seconds, double frame, bool repeatable = true,
                                double speed = 1.0) {
        std::vector<double> events;
        for (double t = 0; t <= seconds + 1e-9; t += frame) {
            KeyEvent e = k.update(true, t, repeatable, speed);
            if (e != KeyEvent::None) {
                events.push_back(t);
            }
        }
        return events;
    }

    bool near(double a, double b, double tol) {
        return a > b - tol && a < b + tol;
    }

    int eventsBetween(const std::vector<double> &ev, double from, double to) {
        int n = 0;
        for (double e: ev) {
            n += e >= from && e < to;
        }
        return n;
    }

    // largest gap between consecutive events inside [from, to)
    double gapAround(const std::vector<double> &ev, double when) {
        for (size_t i = 1; i < ev.size(); i++) {
            if (ev[i] >= when) {
                return ev[i] - ev[i - 1];
            }
        }
        return -1.0;
    }
}

TEST(repeat_curve_matches_the_specification) {
    RepeatTiming t;
    CHECK(near(t.initialDelay, 0.30, 1e-9));
    CHECK(near(repeatInterval(t, 0.31), 0.090, 1e-9));
    CHECK(near(repeatInterval(t, 0.99), 0.090, 1e-9));
    CHECK(near(repeatInterval(t, 1.0), 0.060, 1e-9));
    CHECK(near(repeatInterval(t, 1.99), 0.060, 1e-9));
    CHECK(near(repeatInterval(t, 2.0), 0.040, 1e-9));
    CHECK(near(repeatInterval(t, 3.49), 0.040, 1e-9));
    CHECK(near(repeatInterval(t, 3.5), 0.030, 1e-9));
    CHECK(near(repeatInterval(t, 600), 0.030, 1e-9));
    // the fastest stage is never faster than ~one step per 60 Hz frame pair: stable, never a skip
    CHECK(t.stages[RepeatTiming::STAGES - 1].interval >= 0.028);
}

TEST(repeat_tap_is_one_step) {
    KeyRepeater k;
    CHECK(k.update(true, 10.0, true) == KeyEvent::Press);
    int extra = 0;
    for (double t = 10.0 + 1.0 / 60; t < 10.25; t += 1.0 / 60) {   // a 250 ms tap
        extra += k.update(true, t, true) != KeyEvent::None;
    }
    CHECK_EQ(extra, 0);
    CHECK(k.update(false, 10.26, true) == KeyEvent::None);
}

TEST(repeat_hold_timings_300ms_1s_2s_4s_10s) {
    const double frame = 1.0 / 60;
    KeyRepeater k;
    std::vector<double> ev = holdFor(k, 10.0, frame);
    CHECK(near(ev[0], 0, 1e-9));
    // 300 ms: the first repeat
    CHECK(near(ev[1], 0.30, frame + 1e-6));
    CHECK_EQ(eventsBetween(ev, 0.0, 0.29), 1);
    // stage gaps (one frame of rounding)
    CHECK(near(gapAround(ev, 0.8), 0.090, frame + 1e-6));
    CHECK(near(gapAround(ev, 1.5), 0.060, frame + 1e-6));
    CHECK(near(gapAround(ev, 3.0), 0.040, frame + 1e-6));
    CHECK(near(gapAround(ev, 6.0), 0.030, frame + 1e-6));
    // counts per stage: ~0.7 / 0.09, 1 / 0.06, 1.5 / 0.04, 6.5 / 0.03 (+-1 for frame rounding)
    int s1 = eventsBetween(ev, 0.30, 1.0), s2 = eventsBetween(ev, 1.0, 2.0), s3 = eventsBetween(ev, 2.0, 3.5),
            s4 = eventsBetween(ev, 3.5, 10.0 + 1e-9);
    CHECK(s1 >= 7 && s1 <= 9);
    CHECK(s2 >= 15 && s2 <= 18);
    CHECK(s3 >= 36 && s3 <= 39);
    CHECK(s4 >= 210 && s4 <= 218);
    std::printf("     hold 10 s at 60 fps: %zu steps (0.3-1 s %d, 1-2 s %d, 2-3.5 s %d, 3.5-10 s %d)\n", ev.size(), s1,
                s2, s3, s4);
    // at most one step per frame, always
    for (size_t i = 1; i < ev.size(); i++) {
        CHECK(ev[i] - ev[i - 1] > frame - 1e-6);
    }
    // 4 s hold: past the last stage
    KeyRepeater k4;
    std::vector<double> ev4 = holdFor(k4, 4.0, frame);
    CHECK(near(gapAround(ev4, 3.8), 0.030, frame + 1e-6));
    // 30 fps: the 30 / 40 ms stages run at the frame rate, no backlog builds up
    KeyRepeater slow;
    std::vector<double> ev30 = holdFor(slow, 10.0, 1.0 / 30);
    for (size_t i = 1; i < ev30.size(); i++) {
        CHECK(ev30[i] - ev30[i - 1] > 1.0 / 30 - 1e-6);
    }
    CHECK(eventsBetween(ev30, 5.0, 10.0) <= 151);
}

TEST(repeat_release_stops_at_once_and_reverse_is_immediate) {
    const double frame = 1.0 / 60;
    KeyRepeater down, up;
    double t = 0;
    for (; t < 5.0; t += frame) {
        down.update(true, t, true);
        up.update(false, t, true);
    }
    // release Down: nothing more, ever (no queued repeats)
    int after = 0;
    for (double u = t; u < t + 2.0; u += frame) {
        after += down.update(false, u, true) != KeyEvent::None;
    }
    CHECK_EQ(after, 0);
    // reverse: Up pressed in the same frame Down is released moves at once
    KeyRepeater d2, u2;
    for (t = 0; t < 3.0; t += frame) {
        d2.update(true, t, true);
    }
    CHECK(d2.update(false, t, true) == KeyEvent::None);
    CHECK(u2.update(true, t, true) == KeyEvent::Press);
    // ...and starts its own curve from the beginning (precise again after a fast hold)
    CHECK(u2.update(true, t + 0.29, true) == KeyEvent::None);
    CHECK(u2.update(true, t + 0.30, true) == KeyEvent::Repeat);
}

TEST(repeat_no_burst_after_slow_frames) {
    KeyRepeater k;
    CHECK(k.update(true, 0, true) == KeyEvent::Press);
    CHECK(k.update(true, 4.0, true) == KeyEvent::Repeat);
    // a 2-second hitch: exactly one event, then the normal cadence resumes from now
    CHECK(k.update(true, 6.0, true) == KeyEvent::Repeat);
    CHECK(k.update(true, 6.001, true) == KeyEvent::None);
    CHECK(k.update(true, 6.0 + 0.030 + 0.001, true) == KeyEvent::Repeat);
    // frames of 250 ms for 5 s: one step per frame at most
    KeyRepeater s;
    int n = 0;
    for (double t = 0; t <= 5.0; t += 0.25) {
        n += s.update(true, t, true) != KeyEvent::None;
    }
    CHECK(n <= 21);
    // non-repeatable buttons (Cross, Circle...) only ever press once
    KeyRepeater c;
    CHECK_EQ(holdFor(c, 5.0, 1.0 / 60, false).size(), (size_t) 1);
}

TEST(repeat_analog_full_deflection_is_faster_and_predictable) {
    StickFilter f;
    CHECK(f.update(0, 20000) == StickDir::Down);   // ~61 %: normal
    CHECK(!f.fast() && near(f.speed(), 1.0, 1e-9));
    f.update(0, 25000);                              // 76 %: below FAST_ENGAGE
    CHECK(!f.fast());
    f.update(0, 28000);                              // 85 %: fast
    CHECK(f.fast() && f.speed() > 1.2);
    f.update(0, 25000);                              // hysteresis: still fast above FAST_RELEASE
    CHECK(f.fast());
    f.update(0, 22000);                              // below FAST_RELEASE: normal again
    CHECK(!f.fast());
    f.update(0, 32767);                              // full
    CHECK(f.fast());
    // reversal through the centre and straight to full the other way
    CHECK(f.update(0, -32767) == StickDir::Up);
    CHECK(f.fast());
    // release
    CHECK(f.update(0, 3000) == StickDir::None);
    CHECK(!f.fast());
    // a full-deflection hold makes more steps than the d-pad, but still one per frame at most
    KeyRepeater pad, stick;
    std::vector<double> a = holdFor(pad, 10.0, 1.0 / 60, true, 1.0);
    std::vector<double> b = holdFor(stick, 10.0, 1.0 / 60, true, StickFilter::FAST_SPEED);
    CHECK(b.size() > a.size());
    for (size_t i = 1; i < b.size(); i++) {
        CHECK(b[i] - b[i - 1] > 1.0 / 60 - 1e-6);
    }
    // the initial delay is the same: a quick flick stays one step
    CHECK(near(b[1], 0.30, 1.0 / 60 + 1e-6));
    std::printf("     10 s hold at 60 fps: d-pad %zu steps, stick at full deflection %zu steps\n", a.size(), b.size());
}

// 10-second holds on lists / grids of the real catalog sizes: logical focus never leaves the list, moves at
// most one step per frame, the viewport keeps the focus on screen and stops when the button is released.
TEST(hold_navigation_on_large_lists) {
    struct Case {
        const char *name;
        int count;
        int columns;   // 1 = list
        int visible;   // rows on screen
    };
    const Case cases[] = {{"Live channels", 573, 1, 9}, {"Movies grid", 19797, 7, 3}, {"Series grid", 2337, 7, 3},
                          {"M3U channels", 25000, 1, 9}};
    for (const Case &c: cases) {
        for (int pass = 0; pass < 3; pass++) {   // 0 Down, 1 Up (from the end), 2 analog full deflection Down
            const double frame = 1.0 / 60;
            KeyRepeater k;
            int sel = pass == 1 ? c.count - 1 : 0;
            int first = 0;
            int totalRows = (c.count - 1) / c.columns + 1;
            scroll::Smooth smooth;
            smooth.snap((float) scroll::firstVisible(sel / c.columns, 0, c.visible, totalRows, 1));
            first = (int) smooth.pos;
            int moves = 0, maxPerFrame = 0;
            bool inBounds = true, focusOnScreen = true;
            double speed = pass == 2 ? StickFilter::FAST_SPEED : 1.0;
            double t = 0;
            for (; t <= 10.0; t += frame) {
                int movesThisFrame = 0;
                if (k.update(true, t, true, speed) != KeyEvent::None) {
                    int target = c.columns == 1 ? std::min(std::max(sel + (pass == 1 ? -1 : 1), 0), c.count - 1)
                                                : scroll::gridMove(sel, 0, pass == 1 ? -1 : 1, c.columns, c.count);
                    if (target >= 0 && target != sel) {
                        sel = target;
                        moves++;
                        movesThisFrame++;
                    }
                }
                maxPerFrame = std::max(maxPerFrame, movesThisFrame);
                inBounds = inBounds && sel >= 0 && sel < c.count;
                first = scroll::firstVisible(sel / c.columns, first, c.visible, totalRows, 1);
                smooth.step((float) first, frame);
                int row = sel / c.columns;
                // the drawn window [pos, pos + visible) always contains the focused row (partially at worst)
                focusOnScreen = focusOnScreen && row + 1 > smooth.pos && row < smooth.pos + (float) c.visible;
            }
            // release: no further logical movement, the viewport settles within 0.3 s
            int afterRelease = 0;
            for (double u = t; u < t + 0.30; u += frame) {
                afterRelease += k.update(false, u, true) != KeyEvent::None;
                smooth.step((float) first, frame);
            }
            CHECK(inBounds);
            CHECK(maxPerFrame <= 1);
            CHECK(focusOnScreen);
            CHECK_EQ(afterRelease, 0);
            CHECK(smooth.settled((float) first));
            CHECK(moves > 250);   // ~270 steps in 10 s (d-pad) - far faster than the old curve's ~170
            if (pass != 1) {
                std::printf("     %s (%d): 10 s %s hold -> %d steps, focus %d\n", c.name, c.count,
                            pass == 2 ? "full-stick" : "Down", moves, sel);
            }
        }
    }
}

TEST(smooth_viewport_easing) {
    using scroll::Smooth;
    Smooth s;
    s.snap(10);
    CHECK(s.settled(10));
    // frame-rate independent: 60 fps and 120 fps reach the same place after the same time
    Smooth a, b;
    a.snap(0);
    b.snap(0);
    for (int i = 0; i < 6; i++) {
        a.step(1, 1.0 / 60);
    }
    for (int i = 0; i < 12; i++) {
        b.step(1, 1.0 / 120);
    }
    CHECK(std::fabs(a.pos - b.pos) < 0.002f);
    CHECK(a.pos > 0.8f && a.pos < 1.0f);   // most of the way after 100 ms
    // never overshoots, settles exactly
    Smooth c;
    c.snap(0);
    for (int i = 0; i < 60; i++) {
        c.step(1, 1.0 / 60);
        CHECK(c.pos <= 1.0f);
    }
    CHECK(c.settled(1));
    // never lags more than one row (page jumps, fast repeats)
    Smooth d;
    d.snap(0);
    d.step(40, 1.0 / 60);
    CHECK(d.pos >= 39.0f && d.pos < 40.0f);
    d.step(0, 1.0 / 60);   // reversal: from the other side, still within a row
    CHECK(d.pos <= 1.0f && d.pos >= 0.0f);
    // a slow frame (2 s) does not jump past the target or leave the lag bound
    Smooth e;
    e.snap(5);
    e.step(6, 2.0);
    CHECK(e.pos > 5.0f && e.pos <= 6.0f);
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

TEST(grid_navigation_math) {
    // 8 columns, 20 items: rows 0-7, 8-15, 16-19
    CHECK_EQ(scroll::gridMove(0, -1, 0, 8, 20), -1);    // left edge: focus may leave the grid
    CHECK_EQ(scroll::gridMove(0, 1, 0, 8, 20), 1);
    CHECK_EQ(scroll::gridMove(7, 1, 0, 8, 20), -1);     // right edge does not wrap
    CHECK_EQ(scroll::gridMove(19, 1, 0, 8, 20), -1);    // last item
    CHECK_EQ(scroll::gridMove(3, 0, -1, 8, 20), -1);    // top edge
    CHECK_EQ(scroll::gridMove(3, 0, 1, 8, 20), 11);
    CHECK_EQ(scroll::gridMove(14, 0, 1, 8, 20), 19);    // into the shorter last row: its last item
    CHECK_EQ(scroll::gridMove(18, 0, 1, 8, 20), -1);    // bottom edge
    CHECK_EQ(scroll::gridMove(18, 0, -1, 8, 20), 10);
    CHECK_EQ(scroll::gridMove(0, 0, 1, 8, 0), -1);
    // page jumps keep the column
    CHECK_EQ(scroll::gridPage(2, 1, 3, 8, 19797), 26);
    CHECK_EQ(scroll::gridPage(26, -1, 3, 8, 19797), 2);
    CHECK_EQ(scroll::gridPage(5, -1, 3, 8, 19797), 5);   // already on the first page: first row
    CHECK_EQ(scroll::gridPage(19790, 1, 3, 8, 19797), 19796);
    CHECK_EQ(scroll::gridPage(3, 5, 3, 8, 20), 19);
}
