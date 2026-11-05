#include <algorithm>
#include <cstring>

#include "keyboard_logic.h"

namespace {

    struct KeyDef {
        int usage;
        const char *lower;
        const char *upper;
        const char *altGr;   // "" = none
        bool letter;         // Caps Lock applies
    };

    // US (ANSI / ISO): printable keys besides the letters a..z (usages 4..29)
    const KeyDef US_KEYS[] = {
            {30, "1", "!", "", false}, {31, "2", "@", "", false}, {32, "3", "#", "", false},
            {33, "4", "$", "", false}, {34, "5", "%", "", false}, {35, "6", "^", "", false},
            {36, "7", "&", "", false}, {37, "8", "*", "", false}, {38, "9", "(", "", false},
            {39, "0", ")", "", false}, {44, " ", " ", "", false},
            {45, "-", "_", "", false}, {46, "=", "+", "", false}, {47, "[", "{", "", false},
            {48, "]", "}", "", false}, {49, "\\", "|", "", false}, {50, "\\", "|", "", false},
            {51, ";", ":", "", false}, {52, "'", "\"", "", false}, {53, "`", "~", "", false},
            {54, ",", "<", "", false}, {55, ".", ">", "", false}, {56, "/", "?", "", false},
            {100, "\\", "|", "", false}};

    // Turkish Q (the common Turkish PC layout); UTF-8 for the Turkish letters
    const KeyDef TR_KEYS[] = {
            {30, "1", "!", ">", false}, {31, "2", "'", "\xC2\xA3", false}, {32, "3", "^", "#", false},
            {33, "4", "+", "$", false}, {34, "5", "%", "\xC2\xBD", false}, {35, "6", "&", "", false},
            {36, "7", "/", "{", false}, {37, "8", "(", "[", false}, {38, "9", ")", "]", false},
            {39, "0", "=", "}", false}, {44, " ", " ", "", false},
            {45, "*", "?", "\\", false}, {46, "-", "_", "|", false},
            {47, "\xC4\x9F", "\xC4\x9E", "", true},          // ğ Ğ
            {48, "\xC3\xBC", "\xC3\x9C", "~", true},          // ü Ü
            {49, ",", ";", "`", false}, {50, ",", ";", "`", false},
            {51, "\xC5\x9F", "\xC5\x9E", "", true},          // ş Ş
            {52, "i", "\xC4\xB0", "", true},                  // i İ
            {53, "\"", "\xC3\xA9", "<", false},               // " é
            {54, "\xC3\xB6", "\xC3\x96", "", true},          // ö Ö
            {55, "\xC3\xA7", "\xC3\x87", "", true},          // ç Ç
            {56, ".", ":", "", false}, {100, "<", ">", "|", false}};

    // the keypad types the same on both layouts (digits: Num Lock is not needed for a URL or a MAC address)
    const KeyDef KEYPAD[] = {
            {84, "/", "/", "", false}, {85, "*", "*", "", false}, {86, "-", "-", "", false},
            {87, "+", "+", "", false}, {89, "1", "1", "", false}, {90, "2", "2", "", false},
            {91, "3", "3", "", false}, {92, "4", "4", "", false}, {93, "5", "5", "", false},
            {94, "6", "6", "", false}, {95, "7", "7", "", false}, {96, "8", "8", "", false},
            {97, "9", "9", "", false}, {98, "0", "0", "", false}, {99, ".", ".", "", false}};

    template<size_t N>
    const KeyDef *findIn(const KeyDef (&table)[N], int usage) {
        for (const KeyDef &k: table) {
            if (k.usage == usage) {
                return &k;
            }
        }
        return nullptr;
    }

    bool isRepeating(kbd::Key k) {
        switch (k) {
            case kbd::Key::Char:
            case kbd::Key::Backspace:
            case kbd::Key::Delete:
            case kbd::Key::Left:
            case kbd::Key::Right:
            case kbd::Key::Up:
            case kbd::Key::Down:
            case kbd::Key::PageUp:
            case kbd::Key::PageDown:
                return true;
            default:
                return false;   // Enter / Escape / Tab never repeat (one confirmation per press)
        }
    }
}

namespace kbd {

    Layout layoutFor(const std::string &setting, const std::string &uiLanguage) {
        if (setting == "tr") {
            return Layout::TurkishQ;
        }
        if (setting == "us") {
            return Layout::Us;
        }
        return uiLanguage == "tr" ? Layout::TurkishQ : Layout::Us;
    }

    std::string translate(Layout layout, int usage, bool shift, bool caps, bool altGr) {
        KeyDef letter = {0, "", "", "", true};
        char lowerBuf[2] = {0, 0};
        char upperBuf[2] = {0, 0};
        const KeyDef *def = nullptr;
        if (usage >= USAGE_A && usage < USAGE_A + 26) {
            lowerBuf[0] = (char) ('a' + usage - USAGE_A);
            upperBuf[0] = (char) ('A' + usage - USAGE_A);
            letter.usage = usage;
            letter.lower = lowerBuf;
            letter.upper = upperBuf;
            if (layout == Layout::TurkishQ) {
                if (usage == USAGE_I) {
                    letter.lower = "\xC4\xB1";   // ı (dotless) on the I key
                    letter.upper = "I";
                } else if (usage == USAGE_Q) {
                    letter.altGr = "@";
                } else if (usage == USAGE_A + ('e' - 'a')) {
                    letter.altGr = "\xE2\x82\xAC";   // €
                } else if (usage == USAGE_A + ('t' - 'a')) {
                    letter.altGr = "\xE2\x82\xBA";   // ₺
                }
            }
            def = &letter;
        } else if ((def = findIn(KEYPAD, usage)) == nullptr) {
            def = layout == Layout::TurkishQ ? findIn(TR_KEYS, usage) : findIn(US_KEYS, usage);
        }
        if (def == nullptr) {
            return "";
        }
        if (altGr) {
            return def->altGr;
        }
        bool upper = def->letter ? shift != caps : shift;
        return upper ? def->upper : def->lower;
    }

    bool editingKey(int usage, Key &out) {
        switch (usage) {
            case USAGE_ENTER:
            case USAGE_KP_ENTER:
                out = Key::Enter;
                return true;
            case USAGE_ESCAPE:
                out = Key::Escape;
                return true;
            case USAGE_BACKSPACE:
                out = Key::Backspace;
                return true;
            case USAGE_TAB:
                out = Key::Tab;
                return true;
            case 74:
                out = Key::Home;
                return true;
            case 75:
                out = Key::PageUp;
                return true;
            case 76:
                out = Key::Delete;
                return true;
            case 77:
                out = Key::End;
                return true;
            case 78:
                out = Key::PageDown;
                return true;
            case USAGE_RIGHT:
                out = Key::Right;
                return true;
            case USAGE_LEFT:
                out = Key::Left;
                return true;
            case USAGE_DOWN:
                out = Key::Down;
                return true;
            case USAGE_UP:
                out = Key::Up;
                return true;
            default:
                return false;
        }
    }

    Event Decoder::eventFor(int usage, uint32_t mods, bool caps) const {
        Event e;
        if (editingKey(usage, e.key)) {
            return e;
        }
        bool shift = (mods & (MOD_LSHIFT | MOD_RSHIFT)) != 0;
        bool ctrl = (mods & (MOD_LCTRL | MOD_RCTRL)) != 0;
        bool alt = (mods & (MOD_LALT | MOD_RALT)) != 0;
        bool altGr = (mods & MOD_RALT) != 0 || (ctrl && alt);
        bool meta = (mods & (MOD_LMETA | MOD_RMETA)) != 0;
        if (meta || (ctrl && !altGr) || (alt && !altGr)) {
            return e;   // shortcuts type nothing
        }
        e.key = Key::Char;
        e.text = translate(layout, usage, shift, caps, altGr);
        return e;
    }

    std::vector<Event> Decoder::update(const uint16_t *keys, int count, uint32_t mods, uint32_t locks, double now) {
        std::vector<Event> out;
        std::vector<uint16_t> held;
        for (int i = 0; i < count; i++) {
            uint16_t k = keys[i];
            // 0 = no key, 1..3 = rollover / error codes, 224+ = modifiers (reported in `mods`)
            if (k > 3 && k < 224 && std::find(held.begin(), held.end(), k) == held.end()) {
                held.push_back(k);
            }
        }
        if (locks != 0) {
            locksSeen = true;
        }
        for (uint16_t k: held) {
            bool isNew = std::find(down.begin(), down.end(), k) == down.end();
            if (!isNew) {
                continue;
            }
            if (k == USAGE_CAPS) {
                ownCaps = !ownCaps;
                continue;
            }
            bool caps = locksSeen ? (locks & LOCK_CAPS) != 0 : ownCaps;
            Event e = eventFor(k, mods, caps);
            if (e.key == Key::Char && e.text.empty()) {
                continue;
            }
            out.push_back(e);
            if (isRepeating(e.key)) {
                repeating = k;
                nextRepeat = now + REPEAT_DELAY;
                repeats = 0;
            } else {
                repeating = 0;
            }
        }
        if (repeating != 0 && std::find(held.begin(), held.end(), (uint16_t) repeating) == held.end()) {
            repeating = 0;   // released
        }
        if (repeating != 0 && now >= nextRepeat && out.empty()) {
            bool caps = locksSeen ? (locks & LOCK_CAPS) != 0 : ownCaps;
            Event e = eventFor(repeating, mods, caps);
            if (!(e.key == Key::Char && e.text.empty())) {
                e.repeat = true;
                e.repeatCount = ++repeats;
                out.push_back(e);
            }
            nextRepeat = now + REPEAT_INTERVAL;   // one repeat per snapshot at most (a slow frame never bursts)
        }
        down = std::move(held);
        return out;
    }

    void Decoder::reset() {
        down.clear();
        repeating = 0;
        repeats = 0;
    }

    bool parseState(const unsigned char *raw, size_t size, State &out) {
        out = State();
        if (raw == nullptr || size < STATE_SIZE) {
            return false;
        }
        int32_t n;
        std::memcpy(&n, raw + STATE_NKEYS, sizeof(n));
        std::memcpy(&out.locks, raw + STATE_LOCKS, sizeof(out.locks));
        std::memcpy(&out.mods, raw + STATE_MODS, sizeof(out.mods));
        out.count = n < 0 ? 0 : n > (int32_t) STATE_MAX_KEYS ? (int) STATE_MAX_KEYS : (int) n;
        std::memcpy(out.keys, raw + STATE_KEYS, sizeof(out.keys));
        for (size_t i = (size_t) out.count; i < STATE_MAX_KEYS; i++) {
            out.keys[i] = 0;   // only the reported entries are keys
        }
        return true;
    }

    const std::vector<Event> &Session::poll(double now) {
        events.clear();
        lastChange = Change::None;
        if (handle < 0) {
            if (now < nextOpen || !dev.open) {
                return events;
            }
            nextOpen = now + OPEN_RETRY;
            int h = dev.open();
            if (h < 0) {
                return events;
            }
            handle = h;
            failures = 0;
            keys.reset();
            lastChange = Change::Opened;
        }
        unsigned char raw[BUFFER];
        std::memset(raw, 0, sizeof(raw));
        int rc = dev.read ? dev.read(handle, raw, sizeof(raw)) : -1;
        if (rc < 0) {
            error = rc;
            if (++failures >= READ_FAILURES) {
                if (dev.close) {
                    dev.close(handle);
                }
                handle = -1;
                keys.reset();
                nextOpen = now + OPEN_RETRY;
                lastChange = Change::Disconnected;
            }
            return events;   // a read error is never a key state
        }
        failures = 0;
        State st;
        if (parseState(raw, sizeof(raw), st)) {
            events = keys.update(st.keys, st.count, st.mods, st.locks, now);
        }
        return events;
    }
}
