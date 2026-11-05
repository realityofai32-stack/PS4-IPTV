// Physical (USB / Bluetooth) keyboard: key states -> typed characters and editing keys (host-testable).
//
// libSceKeyboard reports the keys held right now as USB HID usage codes plus modifier and lock bits
// (platform/keyboard.h polls it). The Decoder turns those snapshots into events: a key that appears is pressed,
// a held editing / navigation key or character repeats after REPEAT_DELAY every REPEAT_INTERVAL. The character
// of a key comes from the selected layout (US or Turkish Q) here, not from the system, so typing is the same on
// every firmware: shift, Caps Lock (Turkish letters incl. i/İ and ı/I) and AltGr (@ # $ { [ ] } \ | ~ on the
// Turkish layout) are handled, and every character is UTF-8.

#ifndef PS4IPTV_PLATFORM_KEYBOARD_LOGIC_H
#define PS4IPTV_PLATFORM_KEYBOARD_LOGIC_H

#include <cstdint>
#include <string>
#include <vector>

namespace kbd {

    enum class Layout {
        Us,
        TurkishQ
    };

    // settings value "auto" / "us" / "tr" -> layout; auto follows the UI language ("tr" -> Turkish Q)
    Layout layoutFor(const std::string &setting, const std::string &uiLanguage);

    enum class Key {
        Char,        // `text` holds the typed character (UTF-8)
        Backspace,
        Delete,
        Enter,
        Escape,
        Tab,
        Left,
        Right,
        Up,
        Down,
        Home,
        End,
        PageUp,
        PageDown
    };

    struct Event {
        Key key = Key::Char;
        std::string text;
        bool repeat = false;
        int repeatCount = 0;
    };

    // libSceKeyboard bits (orbis/_types/keyboard.h)
    constexpr uint32_t MOD_LCTRL = 1, MOD_LSHIFT = 2, MOD_LALT = 4, MOD_LMETA = 8;
    constexpr uint32_t MOD_RCTRL = 16, MOD_RSHIFT = 32, MOD_RALT = 64, MOD_RMETA = 128;
    constexpr uint32_t LOCK_NUM = 1, LOCK_CAPS = 2;

    // HID usage codes used by the tests and the mapping
    constexpr int USAGE_A = 4, USAGE_I = 12, USAGE_Q = 20, USAGE_1 = 30, USAGE_0 = 39, USAGE_ENTER = 40,
            USAGE_ESCAPE = 41, USAGE_BACKSPACE = 42, USAGE_TAB = 43, USAGE_SPACE = 44, USAGE_CAPS = 57,
            USAGE_RIGHT = 79, USAGE_LEFT = 80, USAGE_DOWN = 81, USAGE_UP = 82, USAGE_KP_ENTER = 88;

    // the character a key types ("" for keys that type nothing); altGr = right Alt or Ctrl+Alt
    std::string translate(Layout layout, int usage, bool shift, bool caps, bool altGr);

    // the editing / navigation key of a usage code (false for character keys and unknown codes)
    bool editingKey(int usage, Key &out);

    class Decoder {

    public:

        static constexpr double REPEAT_DELAY = 0.5;
        static constexpr double REPEAT_INTERVAL = 0.05;

        void setLayout(Layout l) { layout = l; }

        Layout currentLayout() const { return layout; }

        // keys: usage codes held now (0 entries ignored); now: seconds. Returns the events of this snapshot.
        std::vector<Event> update(const uint16_t *keys, int count, uint32_t mods, uint32_t locks, double now);

        // the keyboard was disconnected: nothing is held any more
        void reset();

    private:

        Event eventFor(int usage, uint32_t mods, bool caps) const;

        Layout layout = Layout::Us;
        std::vector<uint16_t> down;
        int repeating = 0;        // usage code of the key that repeats (the last one pressed), 0 = none
        double nextRepeat = 0;
        int repeats = 0;
        bool locksSeen = false;   // the keyboard reports Caps Lock itself; until then Caps Lock is tracked here
        bool ownCaps = false;
    };
}

#endif // PS4IPTV_PLATFORM_KEYBOARD_LOGIC_H
