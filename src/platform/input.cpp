#include <SDL2/SDL.h>

#include "input.h"

namespace {
    // SDL button indices of PacBrew's SDL2 PS4 joystick driver
    // (src/joystick/ps4/SDL_sysjoystick.c @ bf797a5, verified for the playback test):
    // 0 cross, 1 circle, 2 square, 3 triangle, 4 options, 6 touchpad, 9 L1, 10 R1,
    // 11 up, 12 down, 13 left, 14 right, 15 L2, 16 R2; axes 0 LX, 1 LY.
    const int SDL_INDEX[(int) PadButton::Count] = {11, 12, 13, 14, 0, 1, 2, 3, 9, 10, 15, 16, 4};
}

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

const char *buttonName(PadButton b) {
    static const char *names[] = {"Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle",
                                  "L1", "R1", "L2", "R2", "Options"};
    return (int) b < (int) PadButton::Count ? names[(int) b] : "?";
}

void InputManager::update(void *joystick, double now) {
    pending.clear();
    auto *js = (SDL_Joystick *) joystick;
    bool state[(int) PadButton::Count] = {};
    double speed[(int) PadButton::Count];
    for (double &s: speed) {
        s = 1.0;
    }
    if (js != nullptr) {
        for (int i = 0; i < (int) PadButton::Count; i++) {
            state[i] = SDL_JoystickGetButton(js, SDL_INDEX[i]) != 0;
        }
        // the stick acts as a d-pad: it shares the d-pad buttons' repeat timers. Pushed all the way it scrolls
        // faster (only when the d-pad itself is not held in that direction).
        PadButton stickButton = PadButton::Count;
        switch (stick.update(SDL_JoystickGetAxis(js, 0), SDL_JoystickGetAxis(js, 1))) {
            case input::StickDir::Up:
                stickButton = PadButton::Up;
                break;
            case input::StickDir::Down:
                stickButton = PadButton::Down;
                break;
            case input::StickDir::Left:
                stickButton = PadButton::Left;
                break;
            case input::StickDir::Right:
                stickButton = PadButton::Right;
                break;
            default:
                break;
        }
        if (stickButton != PadButton::Count) {
            if (!state[(int) stickButton]) {
                speed[(int) stickButton] = stick.speed();
            }
            state[(int) stickButton] = true;
        }
    }

    for (int i = 0; i < (int) PadButton::Count; i++) {
        auto b = (PadButton) i;
        input::KeyEvent e = keys[i].update(state[i], now, isRepeatable(b), speed[i]);
        if (e == input::KeyEvent::Press) {
            pending.push_back({b, false, 0});
        } else if (e == input::KeyEvent::Repeat) {
            pending.push_back({b, true, keys[i].repeatCount()});
        }
    }
}
