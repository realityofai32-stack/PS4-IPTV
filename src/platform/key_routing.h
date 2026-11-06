// What a physical keyboard key means where a screen does not take it as text (App::onKey): the controller button
// with the same meaning, so the whole UI can be navigated from the keyboard. Host-testable.

#ifndef PS4IPTV_PLATFORM_KEY_ROUTING_H
#define PS4IPTV_PLATFORM_KEY_ROUTING_H

#include "input.h"
#include "input_actions.h"
#include "keyboard_logic.h"

namespace kbd {

    // The universal keys (platform/input_actions.h routeUniversal): arrows -> d-pad, Enter -> Cross, Escape /
    // Backspace -> Circle (Backspace never repeats: holding it does not leave several screens), Page Up / Down ->
    // L2 / R2, Tab / Shift+Tab -> R1 / L1. false: the key is not universal (screen shortcuts: actions::route).
    inline bool toPad(const Event &k, InputEvent &out) {
        return actions::routeUniversal(k, out);
    }

    // the pre-v7-hints table, kept for reference by the tests (identical to routeUniversal without Tab)
    inline bool toPadClassic(const Event &k, InputEvent &out) {
        out = InputEvent();
        out.repeat = k.repeat;
        out.repeatCount = k.repeatCount;
        switch (k.key) {
            case Key::Up:
                out.button = PadButton::Up;
                return true;
            case Key::Down:
                out.button = PadButton::Down;
                return true;
            case Key::Left:
                out.button = PadButton::Left;
                return true;
            case Key::Right:
                out.button = PadButton::Right;
                return true;
            case Key::Enter:
                out.button = PadButton::Cross;
                return true;
            case Key::Escape:
                out.button = PadButton::Circle;
                return true;
            case Key::Backspace:
                out.button = PadButton::Circle;
                return !k.repeat;
            case Key::PageUp:
                out.button = PadButton::L2;
                return true;
            case Key::PageDown:
                out.button = PadButton::R2;
                return true;
            default:
                return false;
        }
    }
}

#endif // PS4IPTV_PLATFORM_KEY_ROUTING_H
