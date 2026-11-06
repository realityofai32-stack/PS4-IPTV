#include "input_actions.h"

namespace actions {

    namespace {
        KeySpec named(kbd::Key k, bool shift = false) {
            KeySpec s;
            s.kind = KeyKind::Named;
            s.key = k;
            s.shift = shift;
            return s;
        }

        KeySpec letter(char c) {
            KeySpec s;
            s.kind = KeyKind::Letter;
            s.letter = c;
            return s;
        }

        KeySpec typing() {
            KeySpec s;
            s.kind = KeyKind::Typing;
            return s;
        }

        Binding pad(PadButton p, KeySpec k, KeySpec k2 = KeySpec(), PadButton p2 = PadButton::Count) {
            Binding b;
            b.pad = p;
            b.pad2 = p2;
            b.keys[0] = k;
            b.keys[1] = k2;
            return b;
        }

        Binding dpad(KeySpec k, KeySpec k2 = KeySpec()) {
            Binding b;
            b.dpad = true;
            b.keys[0] = k;
            b.keys[1] = k2;
            return b;
        }

        Binding text(Binding b) {
            b.text = true;
            return b;
        }

        // THE binding table: one row per Action (order = enum order, checked by the tests)
        const Binding *table() {
            using K = kbd::Key;
            static const Binding rows[(int) Action::Count] = {
                    pad(PadButton::Cross, named(K::Enter)),                                       // Confirm
                    pad(PadButton::Circle, named(K::Escape)),                                     // Back
                    dpad(named(K::Up), named(K::Down)),                                           // Navigate
                    dpad(named(K::Left), named(K::Right)),                                        // Change
                    pad(PadButton::L1, named(K::Tab), KeySpec(), PadButton::R1),                  // Tabs
                    pad(PadButton::L2, named(K::PageUp), named(K::PageDown), PadButton::R2),      // Page
                    pad(PadButton::Options, letter('o')),                                         // Options
                    pad(PadButton::Triangle, letter('s')),                                        // Search
                    pad(PadButton::Cross, named(K::Enter)),                                       // Details
                    pad(PadButton::Square, letter('f')),                                          // Favorite
                    pad(PadButton::Square, named(K::Delete)),                                     // Remove
                    pad(PadButton::Triangle, named(K::Delete)),                                   // DeleteItem
                    pad(PadButton::Square, letter('e')),                                          // Edit
                    pad(PadButton::Square, letter('d')),                                          // Download
                    pad(PadButton::Square, letter('p')),                                          // PauseResume
                    pad(PadButton::Triangle, letter('o')),                                        // MoreOptions
                    pad(PadButton::Triangle, letter('r')),                                        // Refresh
                    pad(PadButton::Triangle, letter('r')),                                        // Restart
                    pad(PadButton::Triangle, letter('i')),                                        // Info
                    pad(PadButton::Cross, letter(' ')),                                           // PlayPause
                    dpad(named(K::Left), named(K::Right)),                                        // Seek
                    pad(PadButton::L2, named(K::PageUp), named(K::PageDown), PadButton::R2),      // SeekMinute
                    pad(PadButton::R1, letter('n')),                                              // NextEpisode
                    pad(PadButton::L1, named(K::Tab, true)),                                      // PreviousEpisode
                    pad(PadButton::Cross, named(K::Enter)),                                       // PlayNow
                    pad(PadButton::Circle, named(K::Escape)),                                     // WatchCredits
                    text(pad(PadButton::Cross, typing())),                                        // Type
                    text(pad(PadButton::Square, named(K::Backspace))),                            // TextBackspace
                    text(pad(PadButton::Triangle, letter(' '))),                                  // TextSpace
                    pad(PadButton::L2, KeySpec()),                                                // TextShift: the
                                                                                                  // keyboard's own
                    text(pad(PadButton::R2, named(K::Enter))),                                    // TextDone
            };
            return rows;
        }

        const char *NAMES[(int) Action::Count] = {
                "Confirm", "Back", "Navigate", "Change", "Tabs", "Page", "Options", "Search", "Details", "Favorite",
                "Remove", "DeleteItem", "Edit", "Download", "PauseResume", "MoreOptions", "Refresh", "Restart", "Info",
                "PlayPause", "Seek", "SeekMinute", "NextEpisode", "PreviousEpisode", "PlayNow", "WatchCredits", "Type",
                "TextBackspace", "TextSpace", "TextShift", "TextDone"};

        char lower(char c) {
            return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
        }
    }

    const Binding &binding(Action a) {
        int i = (int) a;
        return table()[i >= 0 && i < (int) Action::Count ? i : 0];
    }

    const char *actionName(Action a) {
        int i = (int) a;
        return i >= 0 && i < (int) Action::Count ? NAMES[i] : "?";
    }

    bool hasKeyboard(Action a) {
        return binding(a).keys[0].kind != KeyKind::None;
    }

    bool matches(const KeySpec &spec, const kbd::Event &e) {
        switch (spec.kind) {
            case KeyKind::Named:
                if (e.key != spec.key) {
                    return false;
                }
                return spec.key != kbd::Key::Tab || e.shift == spec.shift;
            case KeyKind::Letter:
                return e.key == kbd::Key::Char && e.text.size() == 1 && lower(e.text[0]) == spec.letter;
            default:
                return false;
        }
    }

    bool routeUniversal(const kbd::Event &k, InputEvent &out) {
        out = InputEvent();
        out.repeat = k.repeat;
        out.repeatCount = k.repeatCount;
        switch (k.key) {
            case kbd::Key::Up:
                out.button = PadButton::Up;
                return true;
            case kbd::Key::Down:
                out.button = PadButton::Down;
                return true;
            case kbd::Key::Left:
                out.button = PadButton::Left;
                return true;
            case kbd::Key::Right:
                out.button = PadButton::Right;
                return true;
            case kbd::Key::Enter:
                out.button = PadButton::Cross;
                return true;
            case kbd::Key::Escape:
                out.button = PadButton::Circle;
                return true;
            case kbd::Key::Backspace:
                out.button = PadButton::Circle;
                return !k.repeat;   // holding it never leaves several screens
            case kbd::Key::PageUp:
                out.button = PadButton::L2;
                return true;
            case kbd::Key::PageDown:
                out.button = PadButton::R2;
                return true;
            case kbd::Key::Tab:
                out.button = k.shift ? PadButton::L1 : PadButton::R1;
                return true;
            default:
                return false;
        }
    }

    bool route(const kbd::Event &k, const std::vector<Action> &declared, InputEvent &out) {
        if (routeUniversal(k, out)) {
            return true;
        }
        for (Action a: declared) {
            const Binding &b = binding(a);
            if (b.text || b.dpad) {
                continue;   // performed by the text field / the arrows themselves
            }
            for (int i = 0; i < 2; i++) {
                if (matches(b.keys[i], k)) {
                    out = InputEvent();
                    out.button = i == 1 && b.pad2 != PadButton::Count ? b.pad2 : b.pad;
                    out.repeat = k.repeat;
                    out.repeatCount = k.repeatCount;
                    return true;
                }
            }
        }
        return false;
    }

    std::string keyLabel(const KeySpec &s, const std::string &space) {
        switch (s.kind) {
            case KeyKind::Letter:
                if (s.letter == ' ') {
                    return space;
                }
                return std::string(1, (char) (s.letter - 'a' + 'A'));
            case KeyKind::Typing:
                return "A-Z";
            case KeyKind::Named:
                switch (s.key) {
                    case kbd::Key::Enter:
                        return "Enter";
                    case kbd::Key::Escape:
                        return "Esc";
                    case kbd::Key::Backspace:
                        return "Backspace";
                    case kbd::Key::Delete:
                        return "Del";
                    case kbd::Key::Tab:
                        return s.shift ? "Shift+Tab" : "Tab";
                    case kbd::Key::PageUp:
                        return "PgUp";
                    case kbd::Key::PageDown:
                        return "PgDn";
                    case kbd::Key::Home:
                        return "Home";
                    case kbd::Key::End:
                        return "End";
                    case kbd::Key::Up:
                        return "\xE2\x86\x91";   // ↑
                    case kbd::Key::Down:
                        return "\xE2\x86\x93";   // ↓
                    case kbd::Key::Left:
                        return "\xE2\x86\x90";   // ←
                    case kbd::Key::Right:
                        return "\xE2\x86\x92";   // →
                    default:
                        return "";
                }
            default:
                return "";
        }
    }

    bool DeviceTracker::set(Device d) {
        if (d == device) {
            return false;
        }
        device = d;
        gen++;
        return true;
    }

    bool DeviceTracker::controllerInput() {
        return set(Device::Controller);
    }

    bool DeviceTracker::keyboardInput() {
        return set(Device::Keyboard);
    }

    bool DeviceTracker::keyboardConnected(bool connected) {
        return !connected && set(Device::Controller);
    }

    DeviceTracker &tracker() {
        static DeviceTracker t;
        return t;
    }
}
