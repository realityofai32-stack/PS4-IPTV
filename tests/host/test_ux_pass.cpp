// The v7-ux-3 pass: the shared choice picker's logic (ui/choice_model.h) with DS4 buttons and keyboard keys, the focus
// states (ui/focus_style.h: one strong focus owner, primary-but-unfocused never looks focused) and the Home
// "Remove from Recently Watched" action (LibraryStore::removeFromRecentlyWatched: only history, never progress /
// favorites / Continue Watching; first / middle / last card; the row re-indexing after a removal).

#include <string>
#include <vector>

#include "check.h"
#include "../../src/platform/input_actions.h"
#include "../../src/storage/library_store.h"
#include "../../src/ui/choice_model.h"
#include "../../src/ui/focus_style.h"

using ui::ChoiceModel;
using iptv::ContentType;
namespace F = ui::focus;

namespace {
    InputEvent pad(PadButton b, bool repeat = false) {
        InputEvent e;
        e.button = b;
        e.repeat = repeat;
        return e;
    }

    // a keyboard key as App::onKey routes it to the picker (the picker declares Confirm / Back: universal keys)
    InputEvent key(kbd::Key k, bool repeat = false) {
        kbd::Event ev;
        ev.key = k;
        ev.repeat = repeat;
        InputEvent out;
        CHECK(actions::route(ev, {actions::Action::Confirm, actions::Action::Back}, out));
        return out;
    }
}

TEST(picker_opens_on_the_current_value_and_confirms) {
    // Aspect ratio: 11 values, the current one (index 3) highlighted and selected first
    ChoiceModel m(11, 3);
    CHECK(m.current() == 3 && m.selected() == 3 && m.isCurrent(3) && !m.isCurrent(4) && !m.done());
    CHECK(m.chosen() == -1);
    CHECK(m.apply(pad(PadButton::Down)) == ChoiceModel::Result::Moved && m.selected() == 4);
    CHECK(m.apply(pad(PadButton::Down, true)) == ChoiceModel::Result::Moved && m.selected() == 5);   // held: repeats
    CHECK(m.apply(pad(PadButton::Up)) == ChoiceModel::Result::Moved && m.selected() == 4);
    CHECK(m.current() == 3);                                   // moving never changes the value in effect
    CHECK(m.apply(pad(PadButton::Cross, true)) == ChoiceModel::Result::None);   // a held Cross never confirms
    CHECK(m.apply(pad(PadButton::Cross)) == ChoiceModel::Result::Confirmed && m.chosen() == 4 && m.done());
    CHECK(m.apply(pad(PadButton::Down)) == ChoiceModel::Result::None && m.chosen() == 4);   // finished
    // the ends: no wrap (a held key never loops around); pages of six (L2 / R2, Page Up / Down)
    ChoiceModel e(9, 0);
    CHECK(e.apply(pad(PadButton::Up)) == ChoiceModel::Result::None && e.selected() == 0);
    CHECK(e.apply(pad(PadButton::R2)) == ChoiceModel::Result::Moved && e.selected() == 6);
    CHECK(e.apply(pad(PadButton::R2)) == ChoiceModel::Result::Moved && e.selected() == 8);
    CHECK(e.apply(pad(PadButton::Down)) == ChoiceModel::Result::None);
    CHECK(e.apply(pad(PadButton::L2)) == ChoiceModel::Result::Moved && e.selected() == 2);
    // no current value (e.g. a zoom not in the list): nothing checked, the selection starts at the top
    ChoiceModel none(5, 17);
    CHECK(none.current() == -1 && none.selected() == 0 && !none.isCurrent(0));
    ChoiceModel neg(5, -1);
    CHECK(neg.current() == -1 && neg.selected() == 0);
}

TEST(picker_cancel_keeps_the_value) {
    ChoiceModel m(3, 1);   // subtitle size: Small Medium Large
    m.apply(pad(PadButton::Down));
    CHECK(m.apply(pad(PadButton::Circle, true)) == ChoiceModel::Result::None);
    CHECK(m.apply(pad(PadButton::Circle)) == ChoiceModel::Result::Cancelled && m.done() && m.chosen() == -1);
    ChoiceModel o(3, 1);
    CHECK(o.apply(pad(PadButton::Options)) == ChoiceModel::Result::Cancelled && o.chosen() == -1);
    // unrelated buttons do nothing
    ChoiceModel u(3, 1);
    for (PadButton b: {PadButton::Left, PadButton::Right, PadButton::Square, PadButton::Triangle, PadButton::L1,
                       PadButton::R1}) {
        CHECK(u.apply(pad(b)) == ChoiceModel::Result::None);
    }
    CHECK(u.selected() == 1 && !u.done());
    // an empty list closes on Cross / Circle without a value
    ChoiceModel empty(0, 0);
    CHECK(empty.apply(pad(PadButton::Cross)) == ChoiceModel::Result::Cancelled && empty.chosen() == -1);
}

TEST(picker_keyboard_navigation) {
    // the same picker with a keyboard: arrows move, Page Up / Down page, Enter chooses, Esc / Backspace cancel
    ChoiceModel m(11, 0);
    CHECK(m.apply(key(kbd::Key::Down)) == ChoiceModel::Result::Moved && m.selected() == 1);
    CHECK(m.apply(key(kbd::Key::Down, true)) == ChoiceModel::Result::Moved && m.selected() == 2);
    CHECK(m.apply(key(kbd::Key::PageDown)) == ChoiceModel::Result::Moved && m.selected() == 8);
    CHECK(m.apply(key(kbd::Key::Up)) == ChoiceModel::Result::Moved && m.selected() == 7);
    CHECK(m.apply(key(kbd::Key::Enter, true)) == ChoiceModel::Result::None);
    CHECK(m.apply(key(kbd::Key::Enter)) == ChoiceModel::Result::Confirmed && m.chosen() == 7);
    ChoiceModel c(4, 2);
    CHECK(c.apply(key(kbd::Key::Escape)) == ChoiceModel::Result::Cancelled && c.chosen() == -1);
    ChoiceModel b(4, 2);
    CHECK(b.apply(key(kbd::Key::Backspace)) == ChoiceModel::Result::Cancelled);
    // the picker's hints (Confirm / Back) have keys on both devices
    CHECK(actions::hasKeyboard(actions::Action::Confirm) && actions::hasKeyboard(actions::Action::Back));
}

TEST(focus_only_the_focused_control_is_strong) {
    const F::Role roles[] = {F::Role::Primary, F::Role::Secondary, F::Role::Danger, F::Role::Chip, F::Role::NavTab,
                             F::Role::Icon, F::Role::Row};
    for (F::Role r: roles) {
        for (int focused = 0; focused < 2; focused++) {
            for (int selected = 0; selected < 2; selected++) {
                for (int enabled = 0; enabled < 2; enabled++) {
                    F::Look l = F::look(r, focused, selected, enabled);
                    CHECK(l.strong == (focused && enabled));
                    // the strongest treatments belong to the focused control only
                    if (!l.strong) {
                        CHECK(l.fill != F::Fill::Accent && l.fill != F::Fill::FocusTint);
                        CHECK(l.edge != F::Edge::Accent && l.text != F::Text::OnAccent);
                    }
                }
            }
        }
    }
    // the strong look is visibly different from every rest look of the same control
    for (F::Role r: roles) {
        F::Look on = F::look(r, true);
        F::Look rest = F::look(r, false);
        F::Look selected = F::look(r, false, true);
        CHECK(on.fill != rest.fill || on.edge != rest.edge);
        CHECK(on.fill != selected.fill || on.edge != selected.edge);
    }
}

TEST(focus_primary_but_unfocused_is_restrained) {
    // the hardware report: "Series" focused in the navigation while the hero's Play kept the full accent
    F::Look play = F::look(F::Role::Primary, false);
    CHECK(play.fill == F::Fill::AccentMuted && play.edge == F::Edge::None && !play.strong);
    CHECK(play.text == F::Text::Strong);                         // still the important action
    F::Look details = F::look(F::Role::Secondary, false);
    CHECK(details.fill == F::Fill::SurfaceElevated && play.fill != details.fill);   // distinct from secondary
    F::Look playFocused = F::look(F::Role::Primary, true);
    CHECK(playFocused.fill == F::Fill::Accent && playFocused.edge == F::Edge::Accent && playFocused.strong);
    // a selected tab / row (the open category while the focus is elsewhere) is restrained the same way
    CHECK(F::look(F::Role::Chip, false, true).fill == F::Fill::AccentMuted);
    CHECK(F::look(F::Role::Row, false, true).edge == F::Edge::Muted);
    CHECK(F::look(F::Role::NavTab, false, true).fill == F::Fill::None);
}

namespace {
    // the controls of Home in one place: navigation sections + icons, hero Play (primary) / Details, rail cards
    struct Control {
        F::Role role;
        bool selected;
    };

    int strongCount(const std::vector<Control> &controls, int focusIndex) {
        int n = 0;
        for (int i = 0; i < (int) controls.size(); i++) {
            n += F::look(controls[(size_t) i].role, i == focusIndex, controls[(size_t) i].selected).strong;
        }
        return n;
    }
}

TEST(focus_exactly_one_strong_owner_on_home) {
    std::vector<Control> home = {{F::Role::NavTab, true},  {F::Role::NavTab, false}, {F::Role::NavTab, false},
                                 {F::Role::NavTab, false}, {F::Role::Icon, false},   {F::Role::Icon, false},
                                 {F::Role::Primary, false}, {F::Role::Secondary, false}, {F::Role::Row, false},
                                 {F::Role::Row, false}};
    for (int f = 0; f < (int) home.size(); f++) {
        CHECK_EQ(strongCount(home, f), 1);
    }
    CHECK_EQ(strongCount(home, -1), 0);   // nothing focused (a modal on top): nothing strong
    // focus on "Series" (index 3): the hero Play (6) is not strong and not accent-filled
    F::Look play = F::look(home[6].role, false, home[6].selected);
    CHECK(!play.strong && play.fill != F::Fill::Accent);
}

namespace {
    HistoryEntry played(ContentType t, const std::string &id, const std::string &series = "", double pos = 0) {
        HistoryEntry h;
        h.type = t;
        h.id = id;
        h.name = "n" + id;
        h.seriesId = series;
        h.position = pos;
        h.duration = pos > 0 ? 2700 : 0;
        return h;
    }

    std::vector<std::string> recentIds(const LibraryStore &lib) {
        std::vector<std::string> v;
        for (const HistoryEntry *h: lib.recentlyWatched(10)) {
            v.push_back(h->id);
        }
        return v;
    }

    // Home's Remove on the focused card (a copy of it, taken before the row is rebuilt), then the row re-index:
    // the focus stays at the same place, clamped to the new last card
    int removeAt(LibraryStore &lib, int index) {
        std::vector<const HistoryEntry *> row = lib.recentlyWatched(10);
        if (index >= (int) row.size()) {
            return index;
        }
        const HistoryEntry focused = *row[(size_t) index];
        CHECK(lib.removeFromRecentlyWatched(focused));
        int n = (int) lib.recentlyWatched(10).size();
        return std::min(index, std::max(0, n - 1));
    }

    // five cards, newest first: live 7, movie m1 (in progress), episode e2 of series s1 (+ an older e1), movie m2, live 9
    LibraryStore library() {
        LibraryStore lib("unused");
        lib.addHistory(played(ContentType::Live, "9"));
        lib.updateProgress(played(ContentType::Movie, "m2", "", 3000));
        lib.updateProgress(played(ContentType::Series, "e1", "s1", 600));
        lib.updateProgress(played(ContentType::Series, "e2", "s1", 900));
        lib.updateProgress(played(ContentType::Movie, "m1", "", 1200));
        lib.addHistory(played(ContentType::Live, "7"));
        lib.toggleFavorite(ContentType::Movie, "m1");
        lib.toggleFavorite(ContentType::Series, "s1");
        lib.toggleFavorite(ContentType::Live, "7");
        return lib;
    }
}

TEST(recent_remove_first_middle_last) {
    {
        LibraryStore lib = library();
        CHECK((recentIds(lib) == std::vector<std::string>{"7", "m1", "e2", "m2", "9"}));
        int focus = removeAt(lib, 0);   // first
        CHECK((recentIds(lib) == std::vector<std::string>{"m1", "e2", "m2", "9"}) && focus == 0);
        focus = removeAt(lib, focus);   // the card that moved into its place, never a skipped one
        CHECK((recentIds(lib) == std::vector<std::string>{"e2", "m2", "9"}));
    }
    {
        LibraryStore lib = library();
        int focus = removeAt(lib, 2);   // middle: the episode card stands for its series - e1 must not take its place
        CHECK((recentIds(lib) == std::vector<std::string>{"7", "m1", "m2", "9"}) && focus == 2);
        CHECK(recentIds(lib)[(size_t) focus] == "m2");
    }
    {
        LibraryStore lib = library();
        int focus = removeAt(lib, 4);   // last: the focus moves to the new last card
        CHECK((recentIds(lib) == std::vector<std::string>{"7", "m1", "e2", "m2"}) && focus == 3);
        removeAt(lib, 3);
        removeAt(lib, 2);
        removeAt(lib, 1);
        focus = removeAt(lib, 0);
        CHECK(recentIds(lib).empty() && focus == 0);
        CHECK(!lib.removeFromRecentlyWatched(played(ContentType::Live, "7")));   // already gone: nothing
    }
}

TEST(recent_remove_keeps_progress_favorites_and_continue_watching) {
    LibraryStore lib = library();
    const HistoryEntry *before = lib.progressOf(ContentType::Movie, "m1");
    CHECK(before && before->position == 1200);
    size_t cw = lib.continueWatching(10).size();
    // remove every card
    while (!lib.recentlyWatched(10).empty()) {
        removeAt(lib, 0);
    }
    // the resume positions, Continue Watching and favorites are untouched
    CHECK(lib.progressOf(ContentType::Movie, "m1") && lib.progressOf(ContentType::Movie, "m1")->position == 1200);
    CHECK(lib.progressOf(ContentType::Series, "e1") && lib.progressOf(ContentType::Series, "e2"));
    CHECK(lib.progressOf(ContentType::Movie, "m2"));
    CHECK_EQ(lib.continueWatching(10).size(), cw);
    CHECK(lib.isFavorite(ContentType::Movie, "m1") && lib.isFavorite(ContentType::Series, "s1")
          && lib.isFavorite(ContentType::Live, "7"));
    // saved and loaded: the removal persists, the progress too
    LibraryStore back("unused");
    std::string err;
    CHECK(back.deserializeHistory(lib.serializeHistory(), &err));
    CHECK(back.recentlyWatched(10).empty() && back.progressOf(ContentType::Movie, "m1"));
    // new playback brings an item back to the row
    lib.updateProgress(played(ContentType::Movie, "m2", "", 3100));
    CHECK((recentIds(lib) == std::vector<std::string>{"m2"}));
    // removing a movie only removes that movie (another movie with progress is not affected)
    LibraryStore one = library();
    CHECK(one.removeFromRecentlyWatched(played(ContentType::Movie, "m1")));
    CHECK((recentIds(one) == std::vector<std::string>{"7", "e2", "m2", "9"}));
}

TEST(recent_remove_with_ds4_and_keyboard) {
    // Home offers Remove on a Recently Watched card: DS4 Square and keyboard Del both reach it, F does not
    const std::vector<actions::Action> home = {actions::Action::Confirm, actions::Action::Remove, actions::Action::Search,
                                               actions::Action::Options, actions::Action::Back};
    CHECK(actions::binding(actions::Action::Remove).pad == PadButton::Square);
    kbd::Event del;
    del.key = kbd::Key::Delete;
    InputEvent out;
    CHECK(actions::route(del, home, out) && out.button == PadButton::Square && !out.repeat);
    del.repeat = true;   // a held Del is a repeat: Home ignores repeats (one card per press)
    CHECK(actions::route(del, home, out) && out.repeat);
    kbd::Event f;
    f.key = kbd::Key::Char;
    f.text = "f";
    CHECK(!actions::route(f, home, out));
    CHECK_EQ(actions::keyLabel(actions::binding(actions::Action::Remove).keys[0]), std::string("Del"));
}
