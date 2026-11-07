// What a button does on a row of an option list (Settings, Playback options) - host-testable, one rule for both.
//
// The D-pad only navigates (v7-ux-5): Up / Down move between rows, Left goes back (to the categories / the previous
// menu), Right opens a submenu row. Left / Right never change a value. A value changes only with Cross / Enter:
//   Toggle (On / Off)        flips in place
//   Choice with 2 values     switches to the other value in place
//   Choice with 3+ values    opens the shared choice picker (screens::makeChoicePicker)
//   Action / track / menu    runs / selects / opens
// Held Cross / Enter never acts (one change per press).

#ifndef PS4IPTV_UI_OPTION_INPUT_H
#define PS4IPTV_UI_OPTION_INPUT_H

#include "../platform/input.h"

namespace ui {

    enum class OptionKind {
        Toggle,
        Choice,
        Action,     // runs something (a track, Reset, Set as default, Clear cache ...)
        Menu        // opens a submenu (Playback options: Audio, Subtitles, Video, Subtitle style)
    };

    enum class OptionCommand {
        None,
        Up,
        Down,
        Back,        // Left: leave the list (categories / the previous menu)
        Toggle,      // Cross on a Toggle
        Cycle,       // Cross on a 2-value Choice
        OpenPicker,  // Cross on a 3+-value Choice
        Activate,    // Cross on an Action, Cross / Right on a Menu
        Cancel       // Circle
    };

    inline OptionCommand optionCommand(const InputEvent &e, OptionKind kind, int choices = 0) {
        switch (e.button) {
            case PadButton::Up:
                return OptionCommand::Up;
            case PadButton::Down:
                return OptionCommand::Down;
            case PadButton::Left:
                return e.repeat ? OptionCommand::None : OptionCommand::Back;
            case PadButton::Right:
                return !e.repeat && kind == OptionKind::Menu ? OptionCommand::Activate : OptionCommand::None;
            case PadButton::Cross:
                if (e.repeat) {
                    return OptionCommand::None;
                }
                switch (kind) {
                    case OptionKind::Toggle:
                        return OptionCommand::Toggle;
                    case OptionKind::Choice:
                        return choices >= 3 ? OptionCommand::OpenPicker : choices == 2 ? OptionCommand::Cycle
                                                                                         : OptionCommand::None;
                    default:
                        return OptionCommand::Activate;
                }
            case PadButton::Circle:
                return e.repeat ? OptionCommand::None : OptionCommand::Cancel;
            default:
                return OptionCommand::None;
        }
    }

    // a value can only change through Cross (never a D-pad direction)
    inline bool changesValue(OptionCommand c) {
        return c == OptionCommand::Toggle || c == OptionCommand::Cycle || c == OptionCommand::OpenPicker;
    }
}

#endif // PS4IPTV_UI_OPTION_INPUT_H
