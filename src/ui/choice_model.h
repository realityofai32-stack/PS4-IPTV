// The choice picker's logic (host-testable): a list of 3+ discrete values, the current one highlighted, Up / Down
// moves, Cross / Enter confirms, Circle / Esc cancels. One picker for every multi-value setting (Settings, Playback
// options); screens::makeChoicePicker draws it. Keyboard keys reach it as the same buttons (actions::routeUniversal:
// arrows, Enter, Esc / Backspace, Page Up / Down).

#ifndef PS4IPTV_UI_CHOICE_MODEL_H
#define PS4IPTV_UI_CHOICE_MODEL_H

#include "../platform/input.h"

namespace ui {

    class ChoiceModel {
    public:
        enum class Result {
            None,
            Moved,
            Confirmed,   // chosen() is the value to apply
            Cancelled
        };

        // current: the value in effect (highlighted, where the selection starts); out of range: none, starts at 0
        ChoiceModel(int count, int current);

        int count() const { return n; }

        int current() const { return cur; }

        int selected() const { return sel; }

        bool isCurrent(int i) const { return i == cur; }

        // the confirmed value, -1 before / without a confirmation
        int chosen() const { return result == Result::Confirmed ? sel : -1; }

        bool done() const { return result == Result::Confirmed || result == Result::Cancelled; }

        // a button (DS4 or a routed key); held Up / Down repeat, Cross / Circle never act on a repeat
        Result apply(const InputEvent &e);

        // rows per page for L2 / R2 (Page Up / Down)
        static const int PAGE = 6;

    private:
        int n;
        int cur;
        int sel;
        Result result = Result::None;
    };
}

#endif // PS4IPTV_UI_CHOICE_MODEL_H
