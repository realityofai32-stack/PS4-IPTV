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
