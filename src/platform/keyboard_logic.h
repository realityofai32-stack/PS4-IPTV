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

#include <cstddef>
#include <cstdint>
#include <functional>
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

    // ------------------------------------------------------------------ the console's state snapshot
    // sceKeyboardReadState fills OrbisKeyboardData (OpenOrbis keyboard sample, reverse-engineered):
    //   0 timestamp u32 | 4 padding[12] | 16 unk1 i32 | 20 nkeys i32 | 24 locks u32 | 28 mods u32 | 32 keycodes u16[32]
    // It is read into a larger zeroed buffer (BUFFER) and decoded field by field from these offsets
    // (platform/keyboard.cpp checks them against the OpenOrbis structure at compile time).
    constexpr size_t STATE_NKEYS = 20, STATE_LOCKS = 24, STATE_MODS = 28, STATE_KEYS = 32, STATE_MAX_KEYS = 32;
    constexpr size_t STATE_SIZE = STATE_KEYS + 2 * STATE_MAX_KEYS;   // 96

    struct State {
        int count = 0;               // valid entries in keys (0..STATE_MAX_KEYS)
        uint32_t locks = 0;
        uint32_t mods = 0;
        uint16_t keys[STATE_MAX_KEYS] = {};
    };

    // false (and an empty state) when `size` cannot hold the structure; nkeys is clamped to 0..STATE_MAX_KEYS
    // (a negative or huge count never reads past the key array)
    bool parseState(const unsigned char *raw, size_t size, State &out);

    // Keyboard connection over a device (the console's libSceKeyboard on the PS4, a fake one in the tests):
    // opens the keyboard (again every OPEN_RETRY seconds until one is there), reads one state per frame into a
    // zeroed BUFFER, closes after READ_FAILURES consecutive read errors (unplugged) and forgets held keys.
    class Session {

    public:

        static constexpr double OPEN_RETRY = 3.0;
        static constexpr int READ_FAILURES = 30;
        static constexpr size_t BUFFER = 512;

        struct Device {
            std::function<int()> open;                                    // handle >= 0, or < 0
            std::function<int(int, unsigned char *, size_t)> read;        // < 0 on error; fills at most `size` bytes
            std::function<void(int)> close;
        };

        enum class Change {
            None,
            Opened,
            Disconnected
        };

        explicit Session(Device device) : dev(std::move(device)) {}

        // key events of this frame
        const std::vector<Event> &poll(double now);

        // what poll() just did to the connection (for the log)
        Change change() const { return lastChange; }

        int lastError() const { return error; }

        bool connected() const { return handle >= 0; }

        Decoder &decoder() { return keys; }

    private:

        Device dev;
        Decoder keys;
        std::vector<Event> events;
        int handle = -1;
        double nextOpen = 0;
        int failures = 0;
        int error = 0;
        Change lastChange = Change::None;
    };
}

#endif // PS4IPTV_PLATFORM_KEYBOARD_LOGIC_H
