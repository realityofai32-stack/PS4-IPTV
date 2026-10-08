#include <cstdlib>

#include "input_logic.h"

namespace input {

    double repeatInterval(const RepeatTiming &t, double heldFor) {
        for (const auto &stage: t.stages) {
            if (heldFor < stage.until) {
                return stage.interval;
            }
        }
        return t.stages[RepeatTiming::STAGES - 1].interval;
    }

    KeyEvent KeyRepeater::update(bool isDown, double now, bool repeatable, double speed) {
        if (!isDown) {
            down = false;
            repeats = 0;
            return KeyEvent::None;
        }
        if (!down) {
            down = true;
            pressedAt = now;
            nextAt = now + timing.initialDelay;
            repeats = 0;
            return KeyEvent::Press;
        }
        if (!repeatable || now < nextAt) {
            return KeyEvent::None;
        }
        repeats++;
        double interval = repeatInterval(timing, now - pressedAt) / (speed > 0.5 ? speed : 1.0);
        nextAt += interval;          // even cadence independent of the frame rate
        if (nextAt <= now) {
            nextAt = now + interval; // after a long frame: continue from now, never catch up in a burst
        }
        return KeyEvent::Repeat;
    }

    StickDir StickFilter::update(int x, int y) {
        int ax = std::abs(x);
        int ay = std::abs(y);
        StickDir before = dir;
        if (dir != StickDir::None) {
            bool vertical = dir == StickDir::Up || dir == StickDir::Down;
            int held = dir == StickDir::Up ? -y : dir == StickDir::Down ? y : dir == StickDir::Left ? -x : x;
            int other = vertical ? ax : ay;
            if (held >= RELEASE && !(other >= ENGAGE && other > held)) {
                isFast = isFast ? held >= FAST_RELEASE : held >= FAST_ENGAGE;
                return dir;
            }
            dir = StickDir::None;  // back near the centre, reversed, or another axis took over
        }
        if (ax >= ENGAGE || ay >= ENGAGE) {
            if (ax > ay) {
                dir = x < 0 ? StickDir::Left : StickDir::Right;
            } else {
                dir = y < 0 ? StickDir::Up : StickDir::Down;
            }
        }
        if (dir == StickDir::None || dir != before) {
            // a new direction starts at normal speed unless it is already pushed all the way
            int held = dir == StickDir::None ? 0 : (dir == StickDir::Up || dir == StickDir::Down ? ay : ax);
            isFast = held >= FAST_ENGAGE;
        }
        return dir;
    }
}
