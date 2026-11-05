// Physical keyboard: HID snapshots -> events (press edges, repeat, modifiers), US and Turkish Q layouts, and typing
// into the text entry model.

#include "check.h"
#include "../../src/platform/keyboard_logic.h"
#include "../../src/ui/keyboard_model.h"

using kbd::Decoder;
using kbd::Event;
using kbd::Layout;

namespace {
    const int USAGE_SLASH = 56, USAGE_PERIOD = 55, USAGE_SEMICOLON = 51, USAGE_QUOTE = 52, USAGE_COMMA = 54;
    const int USAGE_LBRACKET = 47, USAGE_RBRACKET = 48, USAGE_MINUS = 45, USAGE_7 = 36, USAGE_6 = 35, USAGE_0 = 39;

    std::vector<Event> press(Decoder &d, std::vector<uint16_t> keys, uint32_t mods = 0, uint32_t locks = 0, double t = 0) {
        return d.update(keys.data(), (int) keys.size(), mods, locks, t);
    }

    std::string type(Layout l, int usage, uint32_t mods = 0, uint32_t locks = 0) {
        Decoder d;
        d.setLayout(l);
        auto ev = press(d, {(uint16_t) usage}, mods, locks);
        return ev.size() == 1 && ev[0].key == kbd::Key::Char ? ev[0].text : "<none>";
    }
}

TEST(keyboard_us_characters_shift_and_url_symbols) {
    CHECK_EQ(type(Layout::Us, kbd::USAGE_A), std::string("a"));
    CHECK_EQ(type(Layout::Us, kbd::USAGE_A, kbd::MOD_LSHIFT), std::string("A"));
    CHECK_EQ(type(Layout::Us, kbd::USAGE_A, 0, kbd::LOCK_CAPS), std::string("A"));
    CHECK_EQ(type(Layout::Us, kbd::USAGE_A, kbd::MOD_RSHIFT, kbd::LOCK_CAPS), std::string("a"));   // shift undoes caps
    CHECK_EQ(type(Layout::Us, kbd::USAGE_1), std::string("1"));
    CHECK_EQ(type(Layout::Us, kbd::USAGE_1 + 1, kbd::MOD_LSHIFT), std::string("@"));
    CHECK_EQ(type(Layout::Us, USAGE_SLASH), std::string("/"));
    CHECK_EQ(type(Layout::Us, USAGE_SEMICOLON, kbd::MOD_LSHIFT), std::string(":"));
    CHECK_EQ(type(Layout::Us, USAGE_SLASH, kbd::MOD_LSHIFT), std::string("?"));
    CHECK_EQ(type(Layout::Us, USAGE_7, kbd::MOD_LSHIFT), std::string("&"));
    CHECK_EQ(type(Layout::Us, USAGE_MINUS), std::string("-"));
    CHECK_EQ(type(Layout::Us, USAGE_MINUS, kbd::MOD_LSHIFT), std::string("_"));
    CHECK_EQ(type(Layout::Us, kbd::USAGE_SPACE), std::string(" "));
    CHECK_EQ(type(Layout::Us, 89), std::string("1"));   // keypad
    // shortcuts type nothing
    CHECK_EQ(type(Layout::Us, kbd::USAGE_A, kbd::MOD_LCTRL), std::string("<none>"));
    CHECK_EQ(type(Layout::Us, kbd::USAGE_A, kbd::MOD_LALT), std::string("<none>"));
    CHECK_EQ(type(Layout::Us, kbd::USAGE_A, kbd::MOD_LMETA), std::string("<none>"));
}

TEST(keyboard_turkish_q_letters_and_altgr) {
    CHECK_EQ(type(Layout::TurkishQ, kbd::USAGE_I), std::string("\xC4\xB1"));                 // ı
    CHECK_EQ(type(Layout::TurkishQ, kbd::USAGE_I, kbd::MOD_LSHIFT), std::string("I"));
    CHECK_EQ(type(Layout::TurkishQ, USAGE_QUOTE), std::string("i"));
    CHECK_EQ(type(Layout::TurkishQ, USAGE_QUOTE, kbd::MOD_LSHIFT), std::string("\xC4\xB0"));  // İ
    CHECK_EQ(type(Layout::TurkishQ, USAGE_QUOTE, 0, kbd::LOCK_CAPS), std::string("\xC4\xB0"));
    CHECK_EQ(type(Layout::TurkishQ, kbd::USAGE_I, 0, kbd::LOCK_CAPS), std::string("I"));
    CHECK_EQ(type(Layout::TurkishQ, USAGE_SEMICOLON), std::string("\xC5\x9F"));              // ş
    CHECK_EQ(type(Layout::TurkishQ, USAGE_LBRACKET, kbd::MOD_LSHIFT), std::string("\xC4\x9E")); // Ğ
    CHECK_EQ(type(Layout::TurkishQ, USAGE_RBRACKET), std::string("\xC3\xBC"));               // ü
    CHECK_EQ(type(Layout::TurkishQ, USAGE_COMMA), std::string("\xC3\xB6"));                  // ö
    CHECK_EQ(type(Layout::TurkishQ, USAGE_PERIOD, kbd::MOD_LSHIFT), std::string("\xC3\x87")); // Ç
    // URL characters on the Turkish layout
    CHECK_EQ(type(Layout::TurkishQ, USAGE_SLASH), std::string("."));
    CHECK_EQ(type(Layout::TurkishQ, USAGE_SLASH, kbd::MOD_LSHIFT), std::string(":"));
    CHECK_EQ(type(Layout::TurkishQ, USAGE_7, kbd::MOD_LSHIFT), std::string("/"));
    CHECK_EQ(type(Layout::TurkishQ, USAGE_MINUS, kbd::MOD_LSHIFT), std::string("?"));
    CHECK_EQ(type(Layout::TurkishQ, USAGE_6, kbd::MOD_LSHIFT), std::string("&"));
    CHECK_EQ(type(Layout::TurkishQ, USAGE_0, kbd::MOD_LSHIFT), std::string("="));
    CHECK_EQ(type(Layout::TurkishQ, kbd::USAGE_Q, kbd::MOD_RALT), std::string("@"));
    CHECK_EQ(type(Layout::TurkishQ, kbd::USAGE_Q, kbd::MOD_LCTRL | kbd::MOD_LALT), std::string("@"));   // Ctrl+Alt = AltGr
    CHECK_EQ(type(Layout::TurkishQ, kbd::USAGE_1 + 2, kbd::MOD_RALT), std::string("#"));
    CHECK_EQ(type(Layout::TurkishQ, USAGE_MINUS, kbd::MOD_RALT), std::string("\\"));
    // layout setting
    CHECK(kbd::layoutFor("auto", "tr") == Layout::TurkishQ);
    CHECK(kbd::layoutFor("auto", "en") == Layout::Us);
    CHECK(kbd::layoutFor("us", "tr") == Layout::Us);
    CHECK(kbd::layoutFor("tr", "en") == Layout::TurkishQ);
    CHECK(kbd::layoutFor("garbage", "en") == Layout::Us);
}

TEST(keyboard_edges_editing_keys_and_repeat) {
    Decoder d;
    // a key held over several snapshots is one press
    auto e1 = press(d, {kbd::USAGE_A}, 0, 0, 0.0);
    auto e2 = press(d, {kbd::USAGE_A}, 0, 0, 0.1);
    CHECK_EQ(e1.size(), (size_t) 1);
    CHECK(e2.empty());
    // repeat after the delay, at most one per snapshot
    auto e3 = press(d, {kbd::USAGE_A}, 0, 0, 0.55);
    CHECK(e3.size() == 1 && e3[0].repeat && e3[0].text == "a" && e3[0].repeatCount == 1);
    auto e4 = press(d, {kbd::USAGE_A}, 0, 0, 0.56);
    CHECK(e4.empty());
    auto e5 = press(d, {kbd::USAGE_A}, 0, 0, 0.62);
    CHECK(e5.size() == 1 && e5[0].repeatCount == 2);
    // released: nothing more; pressed again: a new press
    CHECK(press(d, {}, 0, 0, 0.7).empty());
    auto e6 = press(d, {kbd::USAGE_A}, 0, 0, 0.8);
    CHECK(e6.size() == 1 && !e6[0].repeat);
    press(d, {}, 0, 0, 0.9);

    // editing keys
    auto bs = press(d, {kbd::USAGE_BACKSPACE}, 0, 0, 1.0);
    CHECK(bs.size() == 1 && bs[0].key == kbd::Key::Backspace);
    auto bsRepeat = press(d, {kbd::USAGE_BACKSPACE}, 0, 0, 1.6);
    CHECK(bsRepeat.size() == 1 && bsRepeat[0].key == kbd::Key::Backspace && bsRepeat[0].repeat);
    press(d, {}, 0, 0, 1.7);
    auto enter = press(d, {kbd::USAGE_ENTER}, 0, 0, 2.0);
    CHECK(enter.size() == 1 && enter[0].key == kbd::Key::Enter);
    CHECK(press(d, {kbd::USAGE_ENTER}, 0, 0, 3.0).empty());   // Enter never repeats (one confirmation)
    press(d, {}, 0, 0, 3.1);
    auto esc = press(d, {kbd::USAGE_ESCAPE}, 0, 0, 3.2);
    CHECK(esc.size() == 1 && esc[0].key == kbd::Key::Escape);
    press(d, {}, 0, 0, 3.3);
    auto kpEnter = press(d, {kbd::USAGE_KP_ENTER}, 0, 0, 3.4);
    CHECK(kpEnter.size() == 1 && kpEnter[0].key == kbd::Key::Enter);
    press(d, {}, 0, 0, 3.5);
    auto arrows = press(d, {kbd::USAGE_UP, kbd::USAGE_LEFT}, 0, 0, 3.6);
    CHECK(arrows.size() == 2 && arrows[0].key == kbd::Key::Up && arrows[1].key == kbd::Key::Left);
    press(d, {}, 0, 0, 3.7);
    // rollover codes and modifier usages are not keys
    CHECK(press(d, {1, 224, 225}, 0, 0, 3.8).empty());
    // Caps Lock tracked by the decoder when the keyboard does not report lock bits
    Decoder c;
    press(c, {kbd::USAGE_CAPS}, 0, 0, 0);
    press(c, {}, 0, 0, 0.1);
    auto upper = press(c, {kbd::USAGE_A}, 0, 0, 0.2);
    CHECK(upper.size() == 1 && upper[0].text == "A");
    // a disconnect forgets held keys
    Decoder r;
    press(r, {kbd::USAGE_A}, 0, 0, 0);
    r.reset();
    CHECK_EQ(press(r, {kbd::USAGE_A}, 0, 0, 0.1).size(), (size_t) 1);
}

TEST(keyboard_typing_into_text_fields) {
    KeyboardModel m("http://", 32);
    CHECK(m.typeText("h"));
    CHECK(m.typeText("\xC4\xB0"));   // İ
    CHECK_EQ(m.text(), std::string("http://h\xC4\xB0"));
    CHECK(!m.typeText("\n"));        // control characters are dropped
    CHECK(!m.typeText(""));
    m.backspace();                   // one code point
    CHECK_EQ(m.text(), std::string("http://h"));
    // the length limit applies to typed text too
    KeyboardModel shortField("", 3);
    CHECK(shortField.typeText("a"));
    CHECK(shortField.typeText("b"));
    CHECK(shortField.typeText("c"));
    CHECK(!shortField.typeText("d"));
    CHECK_EQ(shortField.text(), std::string("abc"));
    // a MAC address typed with the keyboard
    KeyboardModel mac("", 32);
    for (const char *s: {"0", "0", ":", "1", "A"}) {
        mac.typeText(s);
    }
    CHECK_EQ(mac.text(), std::string("00:1A"));
}

#include "../../src/storage/settings_store.h"

TEST(keyboard_layout_setting_persists) {
    SettingsStore s(".");
    CHECK_EQ(s.get().keyboardLayout, std::string("auto"));   // files without it: automatic
    s.get().keyboardLayout = "tr";
    SettingsStore t(".");
    CHECK(t.deserialize(s.serialize(), nullptr));
    CHECK_EQ(t.get().keyboardLayout, std::string("tr"));
    SettingsStore u(".");
    std::string text = s.serialize();
    size_t at = text.find("\"tr\"", text.find("keyboardLayout"));
    CHECK(at != std::string::npos);
    text.replace(at, 4, "\"xx\"");
    CHECK(u.deserialize(text, nullptr));
    CHECK_EQ(u.get().keyboardLayout, std::string("auto"));   // unknown values: automatic
}

// ---------------------------------------------------------------- end to end: HID snapshots -> text fields

#include <algorithm>
#include <cstring>
#include <random>

#include "../../src/iptv/stalker.h"
#include "../../src/platform/key_routing.h"

namespace {
    struct Stroke {
        uint16_t usage;
        uint32_t mods;
    };

    // the key + modifiers that type `ch` on a layout (searching the layout itself: every character of the test
    // strings must be reachable)
    bool strokeFor(Layout l, const std::string &ch, Stroke &out) {
        const uint32_t combos[] = {0, kbd::MOD_LSHIFT, kbd::MOD_RALT};
        for (uint32_t m: combos) {
            for (int u = 4; u < 101; u++) {
                if (kbd::translate(l, u, (m & kbd::MOD_LSHIFT) != 0, false, (m & kbd::MOD_RALT) != 0) == ch) {
                    out = {(uint16_t) u, m};
                    return true;
                }
            }
        }
        return false;
    }

    std::vector<std::string> codePoints(const std::string &s) {
        std::vector<std::string> out;
        for (size_t i = 0; i < s.size();) {
            unsigned char c = (unsigned char) s[i];
            size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            out.push_back(s.substr(i, n));
            i += n;
        }
        return out;
    }

    // types `text` key by key (press, release) into a field model; false when a character is not on the layout
    bool typeInto(KeyboardModel &field, Layout l, const std::string &text, double &t) {
        Decoder d;
        d.setLayout(l);
        for (const auto &ch: codePoints(text)) {
            Stroke s;
            if (!strokeFor(l, ch, s)) {
                std::printf("  not on the layout: %s\n", ch.c_str());
                return false;
            }
            for (const auto &e: d.update(&s.usage, 1, s.mods, 0, t)) {
                field.applyKey(e);
            }
            t += 0.05;
            d.update(nullptr, 0, 0, 0, t);
            t += 0.05;
        }
        return true;
    }

    KeyboardModel::KeyResult pressKey(KeyboardModel &field, uint16_t usage, double &t, uint32_t mods = 0) {
        Decoder d;
        auto ev = d.update(&usage, 1, mods, 0, t);
        t += 0.1;
        return ev.empty() ? KeyboardModel::KeyResult::Unhandled : field.applyKey(ev[0]);
    }
}

TEST(keyboard_every_turkish_letter_both_cases) {
    // ı İ ş Ş ğ Ğ ü Ü ö Ö ç Ç: lower case, Shift, Caps Lock, and Shift + Caps Lock back to lower case
    struct Letter {
        int usage;
        const char *lower;
        const char *upper;
    } letters[] = {{kbd::USAGE_I, "\xC4\xB1", "I"}, {USAGE_QUOTE, "i", "\xC4\xB0"}, {USAGE_SEMICOLON, "\xC5\x9F", "\xC5\x9E"},
                   {USAGE_LBRACKET, "\xC4\x9F", "\xC4\x9E"}, {USAGE_RBRACKET, "\xC3\xBC", "\xC3\x9C"},
                   {USAGE_COMMA, "\xC3\xB6", "\xC3\x96"}, {USAGE_PERIOD, "\xC3\xA7", "\xC3\x87"}};
    for (const auto &l: letters) {
        CHECK_EQ(kbd::translate(Layout::TurkishQ, l.usage, false, false, false), std::string(l.lower));
        CHECK_EQ(kbd::translate(Layout::TurkishQ, l.usage, true, false, false), std::string(l.upper));
        CHECK_EQ(kbd::translate(Layout::TurkishQ, l.usage, false, true, false), std::string(l.upper));
        CHECK_EQ(kbd::translate(Layout::TurkishQ, l.usage, true, true, false), std::string(l.lower));
    }
    // the US layout never types them (the same keys are ASCII punctuation there)
    CHECK_EQ(kbd::translate(Layout::Us, USAGE_SEMICOLON, false, false, false), std::string(";"));
    CHECK_EQ(kbd::translate(Layout::Us, kbd::USAGE_I, false, false, false), std::string("i"));
    CHECK_EQ(kbd::translate(Layout::Us, kbd::USAGE_I, false, true, false), std::string("I"));
}

TEST(keyboard_numbers_punctuation_and_altgr_tables) {
    for (Layout l: {Layout::Us, Layout::TurkishQ}) {
        for (int i = 0; i < 10; i++) {
            CHECK_EQ(kbd::translate(l, kbd::USAGE_1 + i, false, false, false), std::string(1, (char) ('0' + (i + 1) % 10)));
            CHECK_EQ(kbd::translate(l, 89 + (i == 9 ? 9 : i), false, false, false),
                     std::string(1, (char) ('0' + (i + 1) % 10)));   // keypad (Num Lock not needed)
            CHECK(kbd::translate(l, kbd::USAGE_1 + i, false, true, false)
                  == kbd::translate(l, kbd::USAGE_1 + i, false, false, false));   // Caps Lock never shifts digits
        }
        // every URL / MAC / password character is reachable
        for (const char *ch: {":", "/", ".", "?", "&", "=", "@", "-", "_", "%", "#", "+", "~", "!", "*", "(", ")",
                              ",", ";", "[", "]", "{", "}", "$", "<", ">", "|", "\\", "'", "\"", " "}) {
            Stroke s;
            CHECK(strokeFor(l, ch, s));
        }
    }
    // AltGr on the US layout types nothing (no AltGr level), on unknown keys neither
    CHECK_EQ(kbd::translate(Layout::Us, kbd::USAGE_Q, false, false, true), std::string(""));
    CHECK_EQ(kbd::translate(Layout::TurkishQ, kbd::USAGE_A, false, false, true), std::string(""));
    for (const auto &p: std::vector<std::pair<int, std::string>>{{kbd::USAGE_A + ('e' - 'a'), "\xE2\x82\xAC"},
                                                                 {kbd::USAGE_1 + 3, "$"}, {USAGE_7, "{"},
                                                                 {kbd::USAGE_1 + 7, "["}, {kbd::USAGE_1 + 8, "]"},
                                                                 {USAGE_0, "}"}, {46, "|"}, {USAGE_RBRACKET, "~"}}) {
        CHECK_EQ(kbd::translate(Layout::TurkishQ, p.first, false, false, true), p.second);
    }
    // keys without a character: function keys, media keys, out-of-range codes
    for (int u: {58, 69, 70, 71, 72, 73, 101, 120, 135, 200, 223, 300, 0xFFFF, -1}) {
        CHECK_EQ(kbd::translate(Layout::TurkishQ, u, true, true, true), std::string(""));
        CHECK_EQ(kbd::translate(Layout::Us, u, false, false, false), std::string(""));
    }
}

TEST(keyboard_navigation_keys_and_routing) {
    struct K {
        int usage;
        kbd::Key key;
        bool pad;
        PadButton button;
    } keys[] = {{kbd::USAGE_UP, kbd::Key::Up, true, PadButton::Up}, {kbd::USAGE_DOWN, kbd::Key::Down, true, PadButton::Down},
                {kbd::USAGE_LEFT, kbd::Key::Left, true, PadButton::Left},
                {kbd::USAGE_RIGHT, kbd::Key::Right, true, PadButton::Right},
                {kbd::USAGE_ENTER, kbd::Key::Enter, true, PadButton::Cross},
                {kbd::USAGE_KP_ENTER, kbd::Key::Enter, true, PadButton::Cross},
                {kbd::USAGE_ESCAPE, kbd::Key::Escape, true, PadButton::Circle},
                {kbd::USAGE_BACKSPACE, kbd::Key::Backspace, true, PadButton::Circle},
                {75, kbd::Key::PageUp, true, PadButton::L2}, {78, kbd::Key::PageDown, true, PadButton::R2},
                {74, kbd::Key::Home, false, PadButton::Count}, {77, kbd::Key::End, false, PadButton::Count},
                {76, kbd::Key::Delete, false, PadButton::Count}, {kbd::USAGE_TAB, kbd::Key::Tab, false, PadButton::Count}};
    for (const auto &k: keys) {
        Decoder d;
        uint16_t u = (uint16_t) k.usage;
        auto ev = d.update(&u, 1, kbd::MOD_LSHIFT, 0, 0);   // modifiers never turn them into characters
        CHECK(ev.size() == 1 && ev[0].key == k.key);
        InputEvent e;
        bool routed = !ev.empty() && kbd::toPad(ev[0], e);
        CHECK(routed == k.pad);
        if (routed) {
            CHECK(e.button == k.button && !e.repeat);
        }
    }
    // a held arrow repeats as a held d-pad direction; a held Backspace never leaves several screens
    kbd::Event held;
    held.key = kbd::Key::Down;
    held.repeat = true;
    held.repeatCount = 4;
    InputEvent e;
    CHECK(kbd::toPad(held, e) && e.repeat && e.repeatCount == 4 && e.button == PadButton::Down);
    held.key = kbd::Key::Backspace;
    CHECK(!kbd::toPad(held, e));
    kbd::Event ch;
    ch.key = kbd::Key::Char;
    ch.text = "a";
    CHECK(!kbd::toPad(ch, e));   // characters outside a text field do nothing
    // Space types a space (it is a character, not a controller button)
    CHECK_EQ(type(Layout::TurkishQ, kbd::USAGE_SPACE, kbd::MOD_LSHIFT), std::string(" "));
}

TEST(keyboard_repeat_timing_rollover_and_release) {
    Decoder d;
    double t = 0;
    uint16_t a = kbd::USAGE_A, b = kbd::USAGE_A + 1;
    CHECK_EQ(d.update(&a, 1, 0, 0, t).size(), (size_t) 1);
    // nothing before the delay, at 60 fps
    int early = 0;
    for (t = 0.016; t < Decoder::REPEAT_DELAY - 0.001; t += 0.016) {
        early += (int) d.update(&a, 1, 0, 0, t).size();
    }
    CHECK_EQ(early, 0);
    // about 20 repeats per second afterwards, never more than one per snapshot
    int repeats = 0;
    for (; t < Decoder::REPEAT_DELAY + 1.0; t += 0.016) {
        auto ev = d.update(&a, 1, 0, 0, t);
        CHECK(ev.size() <= 1);
        repeats += (int) ev.size();
    }
    CHECK(repeats >= 15 && repeats <= 21);
    // rollover: a second key held with the first takes over the repeat; the first is not pressed again
    uint16_t both[] = {kbd::USAGE_A, (uint16_t) (kbd::USAGE_A + 1)};
    auto ev = d.update(both, 2, 0, 0, t);
    CHECK(ev.size() == 1 && ev[0].text == "b");
    auto later = d.update(both, 2, 0, 0, t + 0.6);
    CHECK(later.size() == 1 && later[0].text == "b" && later[0].repeat);
    // shift pressed during a hold: the repeat follows the modifiers of now
    auto shifted = d.update(both, 2, kbd::MOD_LSHIFT, 0, t + 0.7);
    CHECK(shifted.size() == 1 && shifted[0].text == "B");
    // releasing the repeating key stops the repeat even while the other one stays down
    CHECK(d.update(&a, 1, 0, 0, t + 0.8).empty());
    CHECK(d.update(&a, 1, 0, 0, t + 2.0).empty());
    // the same key twice in one snapshot is one key
    Decoder dup;
    uint16_t twice[] = {b, b};
    CHECK_EQ(dup.update(twice, 2, 0, 0, 0).size(), (size_t) 1);
    // a slow frame never bursts repeats
    Decoder slow;
    slow.update(&a, 1, 0, 0, 0);
    CHECK_EQ(slow.update(&a, 1, 0, 0, 10.0).size(), (size_t) 1);
}

TEST(keyboard_input_reaches_every_text_field) {
    double t = 0;
    struct Field {
        const char *what;
        Layout layout;
        std::string text;
    } fields[] = {
            {"source name", Layout::TurkishQ, "Ev Sineması \xC4\xB0\xC5\x9F\xC3\xA7i Kanalları"},
            {"server URL", Layout::Us, "http://example.com:8080"},
            {"portal URL", Layout::TurkishQ, "http://portal.example.com:8080/stalker_portal/c/"},
            {"username", Layout::Us, "User_01.test"},
            {"password", Layout::Us, "P@ss w0rd!#$%&*()"},
            {"password (Turkish)", Layout::TurkishQ, "\xC5\x9Fifre\xC4\x9E\xC3\x9C" "2026@"},
            {"M3U URL", Layout::Us, "https://example.com/get.php?username=a&password=b&type=m3u_plus&output=ts"},
            {"M3U URL (Turkish)", Layout::TurkishQ, "https://example.com/list.m3u?token=abc~1"},
            {"MAC address", Layout::Us, "00:1a:79:ab:cd:ef"},
            {"User-Agent", Layout::Us, "VLC/3.0.20 LibVLC/3.0.20"},
    };
    for (const auto &f: fields) {
        KeyboardModel field("", 512);   // the keyboard modal every form field opens
        CHECK(typeInto(field, f.layout, f.text, t));
        if (field.text() != f.text) {
            std::printf("  %s: typed \"%s\"\n", f.what, field.text().c_str());
        }
        CHECK(field.text() == f.text);
        CHECK(pressKey(field, kbd::USAGE_ENTER, t) == KeyboardModel::KeyResult::Ok);
    }
    // the MAC field's value goes through the portal form's validation
    KeyboardModel mac("", 512);
    typeInto(mac, Layout::TurkishQ, "001a79abcdef", t);
    std::string normalized;
    CHECK(stalker::normalizeMac(mac.text(), normalized) && normalized == "00:1A:79:AB:CD:EF");
    // editing keys in a field
    KeyboardModel edit("http://", 512);
    CHECK(pressKey(edit, kbd::USAGE_BACKSPACE, t) == KeyboardModel::KeyResult::Edited);
    CHECK_EQ(edit.text(), std::string("http:/"));
    CHECK(pressKey(edit, 76, t) == KeyboardModel::KeyResult::Edited);   // Delete clears
    CHECK(edit.text().empty());
    CHECK(pressKey(edit, kbd::USAGE_BACKSPACE, t) == KeyboardModel::KeyResult::Unchanged);
    CHECK(pressKey(edit, kbd::USAGE_ESCAPE, t) == KeyboardModel::KeyResult::Cancel);
    CHECK(pressKey(edit, kbd::USAGE_TAB, t) == KeyboardModel::KeyResult::Unchanged);
    CHECK(pressKey(edit, kbd::USAGE_UP, t) == KeyboardModel::KeyResult::Unhandled);   // moves the on-screen focus
    CHECK(pressKey(edit, kbd::USAGE_SPACE, t) == KeyboardModel::KeyResult::Edited && edit.text() == " ");
    // a held Backspace empties the field without leaving it; a held Delete clears once
    KeyboardModel held("abcdefghij", 512);
    Decoder d;
    uint16_t bs = kbd::USAGE_BACKSPACE;
    for (double h = 0; h < 2.0; h += 0.016) {
        for (const auto &e: d.update(&bs, 1, 0, 0, 100 + h)) {
            held.applyKey(e);
        }
    }
    CHECK(held.text().empty());
    kbd::Event del;
    del.key = kbd::Key::Delete;
    del.repeat = true;
    KeyboardModel keep("x", 512);
    CHECK(keep.applyKey(del) == KeyboardModel::KeyResult::Unchanged && keep.text() == "x");
    // Search: its own keyboard layout, same typing; Enter shows the results
    KeyboardModel search("", 120, KeyboardModel::Layout::Search);
    CHECK(typeInto(search, Layout::TurkishQ, "Kurtlar Vadisi Pusu", t));
    CHECK_EQ(search.text(), std::string("Kurtlar Vadisi Pusu"));
    CHECK(pressKey(search, kbd::USAGE_ENTER, t) == KeyboardModel::KeyResult::Ok);
    KeyboardModel search2("", 120, KeyboardModel::Layout::Search);
    CHECK(typeInto(search2, Layout::TurkishQ, "\xC3\xA7\x61\x6C\xC4\xB1\xC5\x9Fkan \xC4\xB0nsanlar", t));
    CHECK_EQ(search2.text(), std::string("\xC3\xA7\x61\x6C\xC4\xB1\xC5\x9Fkan \xC4\xB0nsanlar"));
    // the length limit holds for typing (Search: 120 characters)
    KeyboardModel limited("", 5, KeyboardModel::Layout::Search);
    typeInto(limited, Layout::Us, "abcdefgh", t);
    CHECK_EQ(limited.text(), std::string("abcde"));
}

namespace {
    // a fake libSceKeyboard: plugged in or not, with a scripted state buffer
    struct FakeKeyboard {
        bool plugged = false;
        int opens = 0, opened = 0, closes = 0, reads = 0;
        int nextHandle = 7;
        int open = -1;
        std::vector<unsigned char> state = std::vector<unsigned char>(kbd::STATE_SIZE, 0);
        size_t writeBytes = kbd::STATE_SIZE;   // what the "firmware" writes (may be more than the structure)
        int readResult = 0;

        void keys(std::vector<uint16_t> codes, uint32_t mods = 0, int32_t count = -999) {
            std::fill(state.begin(), state.end(), 0);
            int32_t n = count == -999 ? (int32_t) codes.size() : count;
            std::memcpy(&state[kbd::STATE_NKEYS], &n, 4);
            std::memcpy(&state[kbd::STATE_MODS], &mods, 4);
            for (size_t i = 0; i < codes.size() && i < kbd::STATE_MAX_KEYS; i++) {
                std::memcpy(&state[kbd::STATE_KEYS + 2 * i], &codes[i], 2);
            }
        }

        kbd::Session::Device device() {
            kbd::Session::Device d;
            d.open = [this]() {
                opens++;
                if (!plugged) {
                    return -1;
                }
                open = nextHandle++;
                opened++;
                return open;
            };
            d.read = [this](int handle, unsigned char *buf, size_t size) {
                reads++;
                if (!plugged || handle != open) {
                    return -2147352575;   // an error code: unplugged
                }
                std::memcpy(buf, state.data(), std::min(state.size(), std::min(size, writeBytes)));
                return readResult;
            };
            d.close = [this](int handle) {
                closes++;
                if (handle == open) {
                    open = -1;
                }
            };
            return d;
        }
    };
}

TEST(keyboard_state_buffer_parsing_is_bounds_safe) {
    std::vector<unsigned char> buf(kbd::STATE_SIZE, 0);
    kbd::State st;
    CHECK(!kbd::parseState(nullptr, 512, st) && st.count == 0);
    CHECK(!kbd::parseState(buf.data(), kbd::STATE_SIZE - 1, st) && st.count == 0);   // truncated
    CHECK(kbd::parseState(buf.data(), kbd::STATE_SIZE, st) && st.count == 0);
    for (int32_t n: {-1, -2147483647 - 1, 33, 1000, 2147483647}) {
        std::memcpy(&buf[kbd::STATE_NKEYS], &n, 4);
        CHECK(kbd::parseState(buf.data(), buf.size(), st));
        CHECK(st.count == (n < 0 ? 0 : 32));
    }
    // entries past the reported count are not keys
    int32_t two = 2;
    std::memcpy(&buf[kbd::STATE_NKEYS], &two, 4);
    for (size_t i = 0; i < kbd::STATE_MAX_KEYS; i++) {
        uint16_t code = (uint16_t) (kbd::USAGE_A + i % 26);
        std::memcpy(&buf[kbd::STATE_KEYS + 2 * i], &code, 2);
    }
    CHECK(kbd::parseState(buf.data(), buf.size(), st) && st.count == 2 && st.keys[1] == kbd::USAGE_A + 1 && st.keys[2] == 0);
    // unaligned buffers are fine (fields are copied, never cast)
    std::vector<unsigned char> odd(kbd::STATE_SIZE + 1, 0);
    std::memcpy(odd.data() + 1, buf.data(), kbd::STATE_SIZE);
    CHECK(kbd::parseState(odd.data() + 1, kbd::STATE_SIZE, st) && st.count == 2);
    // random buffers (run under AddressSanitizer / UBSan by scripts/test-host-sanitize.sh): never out of bounds,
    // count always in range, every decoded event sane
    std::mt19937 rng(12345);
    Decoder d;
    d.setLayout(Layout::TurkishQ);
    for (int i = 0; i < 20000; i++) {
        size_t size = rng() % 200;
        std::vector<unsigned char> r(size);
        for (auto &c: r) {
            c = (unsigned char) rng();
        }
        if (kbd::parseState(r.data(), r.size(), st)) {
            CHECK(st.count >= 0 && st.count <= 32);
            for (const auto &e: d.update(st.keys, st.count, st.mods, st.locks, i * 0.016)) {
                CHECK(e.key != kbd::Key::Char || (!e.text.empty() && e.text.size() <= 4));
            }
        } else {
            CHECK(size < kbd::STATE_SIZE);
        }
    }
}

TEST(keyboard_hot_plug_and_disconnect) {
    FakeKeyboard fake;
    kbd::Session s(fake.device());
    // no keyboard: one open attempt, then one every OPEN_RETRY seconds (not every frame)
    for (double t = 0; t < 7.0; t += 0.016) {
        CHECK(s.poll(t).empty());
    }
    CHECK(!s.connected() && fake.opens == 3 && fake.reads == 0);
    // plugged in: found at the next attempt, keys arrive
    fake.plugged = true;
    double t = 9.1;
    s.poll(t);
    CHECK(s.connected() && s.change() == kbd::Session::Change::Opened);
    fake.keys({kbd::USAGE_A});
    auto ev = s.poll(t += 0.016);
    CHECK(ev.size() == 1 && ev[0].text == "a");
    // a held key, then the keyboard is unplugged: transient errors keep the connection for READ_FAILURES frames,
    // never produce keys or repeats, then the session closes and forgets the held key
    fake.plugged = false;
    int events = 0;
    for (int i = 0; i < kbd::Session::READ_FAILURES - 1; i++) {
        events += (int) s.poll(t += 0.1).size();
        CHECK(s.connected());
    }
    events += (int) s.poll(t += 0.1).size();
    CHECK(events == 0 && !s.connected() && s.change() == kbd::Session::Change::Disconnected && fake.closes == 1);
    // plugged in again with the same key held: a fresh press (nothing was stuck)
    fake.plugged = true;
    ev = s.poll(t += kbd::Session::OPEN_RETRY + 0.1);   // opened and read in the same frame
    CHECK(s.connected());
    CHECK(ev.size() == 1 && ev[0].text == "a" && !ev[0].repeat);
    CHECK(s.poll(t += 0.016).empty());
    // many connect / disconnect cycles: every open is closed once, events stay sane
    for (int cycle = 0; cycle < 200; cycle++) {
        fake.plugged = cycle % 2 == 0;
        fake.keys({(uint16_t) (kbd::USAGE_A + cycle % 26), 0xFFFF, 3, 1000}, 0, cycle % 7 == 0 ? -5 : 4);
        for (int f = 0; f < kbd::Session::READ_FAILURES + 5; f++) {
            for (const auto &e: s.poll(t += 0.1)) {
                CHECK(e.key != kbd::Key::Char || e.text.size() == 1);
            }
        }
        t += kbd::Session::OPEN_RETRY;
    }
    CHECK(fake.opened > 90);
    CHECK_EQ(fake.opened - fake.closes, s.connected() ? 1 : 0);   // every handle opened is closed exactly once
    // a firmware writing more than the structure stays inside the session's buffer
    FakeKeyboard big;
    big.plugged = true;
    big.state.resize(kbd::Session::BUFFER, 0xAB);
    big.writeBytes = kbd::Session::BUFFER;
    big.keys({kbd::USAGE_A});
    big.state.resize(kbd::Session::BUFFER, 0xAB);
    kbd::Session s2(big.device());
    CHECK(s2.poll(0).size() == 1);
}
