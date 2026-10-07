#include <SDL2/SDL.h>

#include "input.h"

namespace {
    // SDL button indices of PacBrew's SDL2 PS4 joystick driver
    // (src/joystick/ps4/SDL_sysjoystick.c @ bf797a5, verified for the playback test):
    // 0 cross, 1 circle, 2 square, 3 triangle, 4 options, 6 touchpad, 9 L1, 10 R1,
    // 11 up, 12 down, 13 left, 14 right, 15 L2, 16 R2; axes 0 LX, 1 LY.
    const int SDL_INDEX[(int) PadButton::Count] = {11, 12, 13, 14, 0, 1, 2, 3, 9, 10, 15, 16, 4};

    const int STICK_DEADZONE = 16000;   // of 32767: generous, avoids drift-triggered moves

    bool isRepeatable(PadButton b) {
        switch (b) {
            case PadButton::Up:
            case PadButton::Down:
            case PadButton::Left:
            case PadButton::Right:
            case PadButton::L1:
            case PadButton::R1:
            case PadButton::L2:
            case PadButton::R2:
            case PadButton::Square:   // backspace in the keyboard
                return true;
            default:
                return false;
        }
    }

    // initial delay, then faster the longer the button is held (smooth through 500+ rows)
    double repeatInterval(double heldFor) {
        if (heldFor > 2.5) {
            return 0.030;
        }
        if (heldFor > 1.2) {
            return 0.060;
        }
        return 0.110;
    }

    const double FIRST_REPEAT_DELAY = 0.40;
}

const char *buttonName(PadButton b) {
    static const char *names[] = {"Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle",
                                  "L1", "R1", "L2", "R2", "Options"};
    return (int) b < (int) PadButton::Count ? names[(int) b] : "?";
}

void InputManager::update(void *joystick, double now) {
    pending.clear();
    auto *js = (SDL_Joystick *) joystick;
    bool state[(int) PadButton::Count] = {};
    if (js != nullptr) {
        for (int i = 0; i < (int) PadButton::Count; i++) {
            state[i] = SDL_JoystickGetButton(js, SDL_INDEX[i]) != 0;
        }
        int lx = SDL_JoystickGetAxis(js, 0);
        int ly = SDL_JoystickGetAxis(js, 1);
        // dominant axis only: no accidental diagonal moves in lists
        if (lx * lx > ly * ly) {
            state[(int) PadButton::Left] |= lx < -STICK_DEADZONE;
            state[(int) PadButton::Right] |= lx > STICK_DEADZONE;
        } else {
            state[(int) PadButton::Up] |= ly < -STICK_DEADZONE;
            state[(int) PadButton::Down] |= ly > STICK_DEADZONE;
        }
    }

    for (int i = 0; i < (int) PadButton::Count; i++) {
        auto b = (PadButton) i;
        if (state[i] && !down[i]) {
            down[i] = true;
            pressedAt[i] = now;
            nextRepeat[i] = now + FIRST_REPEAT_DELAY;
            repeats[i] = 0;
            pending.push_back({b, false, 0});
        } else if (state[i] && down[i] && isRepeatable(b) && now >= nextRepeat[i]) {
            repeats[i]++;
            nextRepeat[i] = now + repeatInterval(now - pressedAt[i]);
            pending.push_back({b, true, repeats[i]});
        } else if (!state[i]) {
            down[i] = false;
        }
    }
}
