// Control hints follow the active input device (platform/input_actions.h): the binding table, keyboard routing
// (universal keys, declared shortcuts only, text fields), the hint labels, the active-device switching (last real
// event wins, passive polling never switches, a keyboard disconnect falls back to the controller) and the
// missing-keyboard-mapping check over every action the screens use.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/platform/input_actions.h"
#include "../../src/platform/key_routing.h"
#include "../../src/platform/keyboard_logic.h"
#include "../../src/ui/keyboard_model.h"

using actions::Action;
using actions::Device;
using actions::KeyKind;

namespace {
    // the key event a key spec describes (what the keyboard decoder produces for that key)
    kbd::Event eventOf(const actions::KeySpec &k) {
        kbd::Event e;
        if (k.kind == KeyKind::Letter) {
            e.key = kbd::Key::Char;
            e.text = std::string(1, k.letter);
        } else {
            e.key = k.key;
            e.shift = k.shift;
        }
        return e;
    }

    kbd::Event letter(const std::string &t) {
        kbd::Event e;
        e.key = kbd::Key::Char;
        e.text = t;
        return e;
    }

    kbd::Event key(kbd::Key k, bool shift = false) {
        kbd::Event e;
        e.key = k;
        e.shift = shift;
        return e;
    }

    PadButton routed(const kbd::Event &e, const std::vector<Action> &declared) {
        InputEvent out;
        return actions::route(e, declared, out) ? out.button : PadButton::Count;
    }

    std::vector<Action> allActions() {
        std::vector<Action> v;
        for (int i = 0; i < (int) Action::Count; i++) {
            v.push_back((Action) i);
        }
        return v;
    }

    // the hint sets the screens show (kept in step with src/screens: Home, Live TV, Movies / Series, Series Detail,
    // Search, Settings, Local Media, Downloads, Favorites, Profiles, the players, the Next Episode card)
    const std::map<std::string, std::vector<Action>> &screenSets() {
        static const std::map<std::string, std::vector<Action>> sets = {
                {"home", {Action::Confirm, Action::Remove, Action::Search, Action::Options, Action::Back}},
                {"live", {Action::Confirm, Action::Favorite, Action::Tabs, Action::Search, Action::Options, Action::Back}},
                {"browse-categories", {Action::Confirm, Action::Tabs, Action::Search, Action::Options, Action::Back}},
                {"browse-grid", {Action::Details, Action::Favorite, Action::Tabs, Action::Options, Action::Back}},
                {"movie-detail", {Action::Confirm, Action::Favorite, Action::Back}},
                {"series-detail-episodes", {Action::Confirm, Action::Download, Action::Options, Action::Tabs, Action::Back}},
                {"series-detail", {Action::Confirm, Action::Tabs, Action::Favorite, Action::Back}},
                {"search-typing", {Action::Type, Action::TextBackspace, Action::TextSpace, Action::Tabs, Action::Back}},
                {"search-results", {Action::Confirm, Action::Tabs, Action::Page, Action::Back}},
                {"settings", {Action::Confirm, Action::Tabs, Action::Back}},   // Change: the palette's swatches only
                {"local-media", {Action::Confirm, Action::Refresh, Action::Back}},
                {"downloads", {Action::Confirm, Action::PauseResume, Action::MoreOptions, Action::Tabs, Action::Back}},
                {"favorites", {Action::Confirm, Action::Tabs, Action::Remove, Action::Back}},
                {"profiles", {Action::Confirm, Action::Edit, Action::DeleteItem, Action::Options, Action::Back}},
                {"keyboard", {Action::Type, Action::TextBackspace, Action::TextSpace, Action::TextShift, Action::TextDone,
                              Action::Back}},
                {"live-player", {Action::Confirm, Action::Tabs, Action::Favorite, Action::Info, Action::Back}},
                {"vod-player", {Action::PlayPause, Action::Seek, Action::Options, Action::NextEpisode, Action::Back,
                                Action::Info, Action::Favorite, Action::PreviousEpisode, Action::SeekMinute}},
                {"vod-player-options", {Action::Confirm, Action::Back}},
                {"next-episode-card", {Action::PlayNow, Action::Seek, Action::Options, Action::NextEpisode,
                                       Action::WatchCredits, Action::Info, Action::Favorite, Action::PreviousEpisode,
                                       Action::SeekMinute}},
                {"diagnostics", {Action::Confirm, Action::Restart}},
        };
        return sets;
    }
}

TEST(hints_binding_table_is_complete) {
    for (Action a: allActions()) {
        const actions::Binding &b = actions::binding(a);
        CHECK(std::string(actions::actionName(a)) != "?");
        CHECK(b.dpad || b.pad != PadButton::Count);          // every action has a controller binding
        if (b.pad2 != PadButton::Count) {
            CHECK(b.pad != PadButton::Count && b.pad2 != b.pad);
        }
    }
    // the essentials named by the hardware report
    CHECK(actions::binding(Action::Confirm).pad == PadButton::Cross);
    CHECK(actions::binding(Action::Back).pad == PadButton::Circle);
    CHECK(actions::binding(Action::Options).pad == PadButton::Options);
    CHECK(actions::binding(Action::Search).pad == PadButton::Triangle);
    CHECK(actions::binding(Action::Remove).pad == PadButton::Square);
    CHECK(actions::binding(Action::Tabs).pad == PadButton::L1 && actions::binding(Action::Tabs).pad2 == PadButton::R1);
    CHECK(actions::binding(Action::Page).pad == PadButton::L2 && actions::binding(Action::Page).pad2 == PadButton::R2);
    CHECK(actions::binding(Action::NextEpisode).pad == PadButton::R1);
    CHECK(actions::binding(Action::PlayNow).pad == PadButton::Cross);
    CHECK(actions::binding(Action::WatchCredits).pad == PadButton::Circle);
    CHECK(actions::binding(Action::Seek).dpad && actions::binding(Action::Change).dpad);
}

// every action with a keyboard key really performs it: routed to the action's button (shortcuts while the screen
// declares them), the d-pad for the arrows, or the text field's own handling
TEST(hints_keyboard_keys_perform_their_action) {
    for (Action a: allActions()) {
        const actions::Binding &b = actions::binding(a);
        for (int i = 0; i < 2; i++) {
            const actions::KeySpec &k = b.keys[i];
            if (k.kind == KeyKind::None) {
                continue;
            }
            if (b.text) {
                KeyboardModel field("ab", 64);
                kbd::Event e = k.kind == KeyKind::Typing ? letter("x") : eventOf(k);
                KeyboardModel::KeyResult r = field.applyKey(e);
                CHECK(r == KeyboardModel::KeyResult::Edited || r == KeyboardModel::KeyResult::Ok);
                if (a == Action::TextDone) {
                    CHECK(r == KeyboardModel::KeyResult::Ok);
                }
                continue;
            }
            InputEvent out;
            CHECK(actions::route(eventOf(k), {a}, out));
            if (b.dpad) {
                CHECK(out.button == PadButton::Up || out.button == PadButton::Down || out.button == PadButton::Left
                      || out.button == PadButton::Right);
            } else {
                PadButton want = i == 1 && b.pad2 != PadButton::Count ? b.pad2 : b.pad;
                if (a == Action::Tabs) {
                    want = PadButton::R1;   // Tab = next (Shift+Tab = previous, below)
                }
                if (!(out.button == want)) {
                    std::printf("  %s key %d routes to %d, wants %d\n", actions::actionName(a), i, (int) out.button,
                                (int) want);
                }
                CHECK(out.button == want);
            }
        }
    }
    CHECK(routed(key(kbd::Key::Tab, true), {}) == PadButton::L1);
    CHECK(routed(key(kbd::Key::Tab), {}) == PadButton::R1);
    // the seek keys: Left / Right = the d-pad's 10 s, Page Up / Down = L2 / R2's minute
    CHECK(routed(key(kbd::Key::Left), {}) == PadButton::Left && routed(key(kbd::Key::Right), {}) == PadButton::Right);
    CHECK(routed(key(kbd::Key::PageUp), {}) == PadButton::L2 && routed(key(kbd::Key::PageDown), {}) == PadButton::R2);
}

// a shortcut works only where the screen offers that action: never a hidden one
TEST(hints_shortcuts_only_where_declared) {
    const std::vector<Action> home = screenSets().at("home");
    CHECK(routed(key(kbd::Key::Delete), home) == PadButton::Square);         // Remove from Continue Watching
    CHECK(routed(letter("f"), home) == PadButton::Count);                    // F never removes a card on Home
    CHECK(routed(letter("s"), home) == PadButton::Triangle && routed(letter("S"), home) == PadButton::Triangle);
    CHECK(routed(letter("o"), home) == PadButton::Options);
    CHECK(routed(letter("x"), home) == PadButton::Count);
    // the hero focused: Remove is not offered, Delete does nothing
    CHECK(routed(key(kbd::Key::Delete), {Action::Confirm, Action::Search, Action::Options, Action::Back}) == PadButton::Count);
    const std::vector<Action> browse = screenSets().at("browse-grid");
    CHECK(routed(letter("f"), browse) == PadButton::Square && routed(key(kbd::Key::Delete), browse) == PadButton::Count);
    const std::vector<Action> profiles = screenSets().at("profiles");
    CHECK(routed(letter("e"), profiles) == PadButton::Square && routed(key(kbd::Key::Delete), profiles) == PadButton::Triangle);
    const std::vector<Action> downloads = screenSets().at("downloads");
    CHECK(routed(letter("p"), downloads) == PadButton::Square && routed(letter("o"), downloads) == PadButton::Triangle);
    const std::vector<Action> player = screenSets().at("vod-player");
    CHECK(routed(letter(" "), player) == PadButton::Cross);                   // Space: play / pause
    CHECK(routed(letter("n"), player) == PadButton::R1 && routed(letter("i"), player) == PadButton::Triangle);
    CHECK(routed(letter("o"), player) == PadButton::Options);
    // while the Next Episode card is shown Space is not offered (Enter = Play now, Esc = Watch credits)
    const std::vector<Action> card = screenSets().at("next-episode-card");
    CHECK(routed(letter(" "), card) == PadButton::Count);
    CHECK(routed(key(kbd::Key::Enter), card) == PadButton::Cross && routed(key(kbd::Key::Escape), card) == PadButton::Circle);
    // the universal keys work on every screen, declared or not
    for (const auto &s: screenSets()) {
        CHECK(routed(key(kbd::Key::Enter), s.second) == PadButton::Cross);
        CHECK(routed(key(kbd::Key::Escape), s.second) == PadButton::Circle);
        CHECK(routed(key(kbd::Key::Up), s.second) == PadButton::Up);
    }
    // a held Backspace never leaves several screens; a held arrow repeats
    kbd::Event held = key(kbd::Key::Backspace);
    held.repeat = true;
    CHECK(routed(held, {}) == PadButton::Count);
    kbd::Event arrow = key(kbd::Key::Down);
    arrow.repeat = true;
    arrow.repeatCount = 3;
    InputEvent out;
    CHECK(actions::route(arrow, {}, out) && out.repeat && out.repeatCount == 3);
    // text actions are never routed: the field takes the key (an unhandled character does nothing)
    CHECK(routed(letter("a"), screenSets().at("keyboard")) == PadButton::Count);
}

// no screen offers two actions on one key that mean different buttons, and every shown key is truthful
TEST(hints_no_ambiguous_keys_on_a_screen) {
    for (const auto &s: screenSets()) {
        std::map<std::string, PadButton> seen;
        for (Action a: s.second) {
            const actions::Binding &b = actions::binding(a);
            if (b.text || b.dpad) {
                continue;
            }
            for (int i = 0; i < 2; i++) {
                if (b.keys[i].kind == KeyKind::None) {
                    continue;
                }
                std::string label = actions::keyLabel(b.keys[i]);
                PadButton p = i == 1 && b.pad2 != PadButton::Count ? b.pad2 : b.pad;
                auto it = seen.find(label);
                if (it != seen.end() && !(it->second == p)) {
                    std::printf("  %s: key %s means two buttons\n", s.first.c_str(), label.c_str());
                    CHECK(false);
                }
                seen[label] = p;
                // what the hint shows is what the key does on that screen
                PadButton r = routed(eventOf(b.keys[i]), s.second);
                if (a == Action::Tabs) {
                    CHECK(r == PadButton::R1);
                } else {
                    CHECK(r == p);
                }
            }
        }
    }
}

TEST(hints_key_labels) {
    auto first = [](Action a) { return actions::keyLabel(actions::binding(a).keys[0], "Space"); };
    CHECK_EQ(first(Action::Confirm), std::string("Enter"));
    CHECK_EQ(first(Action::Back), std::string("Esc"));
    CHECK_EQ(first(Action::Tabs), std::string("Tab"));
    CHECK_EQ(first(Action::PreviousEpisode), std::string("Shift+Tab"));
    CHECK_EQ(first(Action::Page), std::string("PgUp"));
    CHECK_EQ(actions::keyLabel(actions::binding(Action::Page).keys[1]), std::string("PgDn"));
    CHECK_EQ(first(Action::Remove), std::string("Del"));
    CHECK_EQ(first(Action::Favorite), std::string("F"));
    CHECK_EQ(first(Action::Search), std::string("S"));
    CHECK_EQ(first(Action::Options), std::string("O"));
    CHECK_EQ(first(Action::NextEpisode), std::string("N"));
    CHECK_EQ(first(Action::PlayNow), std::string("Enter"));
    CHECK_EQ(first(Action::WatchCredits), std::string("Esc"));
    CHECK_EQ(actions::keyLabel(actions::binding(Action::PlayPause).keys[0], "Bo\xC5\x9Fluk"), std::string("Bo\xC5\x9Fluk"));
    CHECK_EQ(first(Action::TextBackspace), std::string("Backspace"));
    CHECK_EQ(first(Action::Type), std::string("A-Z"));
    // no keyboard equivalent: the on-screen keyboard's Shift (the physical keyboard has its own); left out of hints
    CHECK(!actions::hasKeyboard(Action::TextShift));
}

// the active device: the last real input event wins; nothing else switches it
TEST(hints_active_device_switching) {
    actions::DeviceTracker t;
    CHECK(t.active() == Device::Controller);
    unsigned g = t.generation();
    // passive polling: no events, a connected keyboard that reports nothing
    for (int frame = 0; frame < 600; frame++) {
        CHECK(!t.keyboardConnected(true));
    }
    CHECK(t.active() == Device::Controller && t.generation() == g);
    // a key: keyboard hints
    CHECK(t.keyboardInput());
    CHECK(t.active() == Device::Keyboard && t.generation() == g + 1);
    // more keys: no change (no flicker, no redraw)
    for (int i = 0; i < 10; i++) {
        CHECK(!t.keyboardInput());
    }
    CHECK(t.generation() == g + 1);
    // a controller press: controller hints again
    CHECK(t.controllerInput());
    CHECK(t.active() == Device::Controller && t.generation() == g + 2);
    CHECK(!t.controllerInput());
    // keyboard again, then it is unplugged: back to the controller at once
    CHECK(t.keyboardInput());
    CHECK(t.keyboardConnected(false));
    CHECK(t.active() == Device::Controller);
    CHECK(!t.keyboardConnected(false));   // already there
    // an unplugged keyboard while the controller is active changes nothing
    CHECK(t.generation() == g + 4);
    // both used alternately: each real event decides, deterministically
    std::vector<int> script = {1, 1, 0, 1, 0, 0, 1};
    for (int s: script) {
        s ? t.keyboardInput() : t.controllerInput();
        CHECK(t.active() == (s ? Device::Keyboard : Device::Controller));
    }
}

// passive input produces no events: an idle stick inside its deadzone / hysteresis, an idle keyboard state
TEST(hints_passive_polling_produces_no_events) {
    input::StickFilter stick;
    for (int i = 0; i < 1000; i++) {
        int drift = (i % 7) * 1500 - 4500;   // a worn stick drifting up to ~27 %
        CHECK(stick.update(drift, -drift) == input::StickDir::None);
    }
    input::KeyRepeater k;
    for (int i = 0; i < 100; i++) {
        CHECK(k.update(false, i * 0.016, true) == input::KeyEvent::None);
    }
    kbd::Decoder d;
    uint16_t none[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 100; i++) {
        CHECK(d.update(none, 6, 0, 0, i * 0.016).empty());
        CHECK(d.update(none, 0, kbd::MOD_LSHIFT, kbd::LOCK_NUM, i * 0.016 + 0.008).empty());   // a modifier alone
    }
}

TEST(hints_shift_tab_from_the_decoder) {
    kbd::Decoder d;
    uint16_t tab = kbd::USAGE_TAB;
    auto ev = d.update(&tab, 1, kbd::MOD_RSHIFT, 0, 0);
    CHECK(ev.size() == 1 && ev[0].key == kbd::Key::Tab && ev[0].shift);
    CHECK(routed(ev[0], {}) == PadButton::L1);
    kbd::Decoder plain;
    ev = plain.update(&tab, 1, 0, 0, 0);
    CHECK(ev.size() == 1 && !ev[0].shift && routed(ev[0], {}) == PadButton::R1);
    // the legacy routing helper is the universal table
    InputEvent e;
    CHECK(kbd::toPad(ev[0], e) && e.button == PadButton::R1);
}

// missing keyboard mapping detection: every action a screen names (A::X in src/screens) has a keyboard key, or is
// on the short list of actions the physical keyboard does natively
TEST(hints_every_screen_action_has_a_keyboard_key) {
    const char *src = std::getenv("PS4IPTV_SOURCE_DIR");
    if (src == nullptr) {
        std::printf("  (PS4IPTV_SOURCE_DIR not set: source scan skipped)\n");
        return;
    }
    std::map<std::string, Action> byName;
    for (Action a: allActions()) {
        byName[actions::actionName(a)] = a;
    }
    const std::set<std::string> native = {"TextShift"};   // the keyboard's own Shift key
    const std::regex use("\\bA::([A-Za-z]+)");
    std::set<std::string> used;
    int files = 0;
    for (const auto &entry: std::filesystem::directory_iterator(std::filesystem::path(src) / "screens")) {
        if (entry.path().extension() != ".cpp") {
            continue;
        }
        files++;
        std::ifstream f(entry.path());
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        for (std::sregex_iterator it(text.begin(), text.end(), use), end; it != end; ++it) {
            used.insert((*it)[1].str());
        }
    }
    CHECK(files > 10);
    CHECK(used.size() >= 20);
    for (const std::string &name: used) {
        auto it = byName.find(name);
        if (it == byName.end()) {
            std::printf("  unknown action A::%s\n", name.c_str());
            CHECK(false);
            continue;
        }
        if (!actions::hasKeyboard(it->second) && !native.count(name)) {
            std::printf("  A::%s has no keyboard key\n", name.c_str());
            CHECK(false);
        }
    }
    // the screens no longer name controller glyphs for hints
    for (const auto &entry: std::filesystem::directory_iterator(std::filesystem::path(src) / "screens")) {
        std::ifstream f(entry.path());
        std::stringstream ss;
        ss << f.rdbuf();
        if (ss.str().find("ui::Glyph::") != std::string::npos) {
            std::printf("  %s still names a controller glyph\n", entry.path().filename().string().c_str());
            CHECK(false);
        }
    }
}
