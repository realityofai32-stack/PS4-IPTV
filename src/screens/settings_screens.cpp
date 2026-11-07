// Settings (main page, Storage & downloads, Diagnostics) and About.
//
// Settings rows: caption on the left, the value in one right-aligned column - a choice (with dots showing
// its position among the options), a switch for On/Off settings, or a short info text for actions. Rows are
// separators on the background; the focused one gets the tinted surface and the accent edge. The column on
// the right (no box) explains the focused setting and its current value. Up/Down move, Left/Right or X
// change a value, X runs an action. Appearance shows the accent presets as a 3 x 3 palette: the D-pad moves
// inside it, X applies a colour. Geometry: ui/layout.h (layout::settings).
//
// Language changes apply at once: the screens are rebuilt (App::applyLanguage) and Settings opens again.

#include <algorithm>

#include "common.h"
#include "../ui/option_input.h"
#include "build_info.h"
#include "../downloads/download_model.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"

using namespace c2d;
using screens::SettingsPage;

namespace {

    // three columns: categories | the category's settings | what the focused setting does
    namespace L = layout::settings;

    const float VALUE_RIGHT = 24;      // right padding of the value column
    const float VALUE_W = 300;
    const float SWITCH_W = 64;
    const float SWITCH_H = 34;
    const float SWATCH_R = 14;
    const float CELL_GAP = 8;          // between palette cells

    enum class Kind {
        Choice,
        Toggle,
        Action,
        Palette     // a row of accent presets (Appearance): Left / Right pick one, X applies it
    };

    struct SettingItem {
        Kind kind = Kind::Action;
        std::string caption;
        std::vector<std::string> options;         // Choice
        std::function<int()> choice;              // Choice: current option index
        std::function<void(int)> setChoice;
        std::function<bool()> toggle;             // Toggle
        std::function<void(bool)> setToggle;
        std::function<std::string()> info;        // Action: short text in the value column
        std::function<void()> run;                // Action, Swatch
        std::function<std::string()> describe;    // text for the description panel
        std::vector<int> swatches;                // Palette: palette::ACCENTS indices, left to right
    };

    struct Category {
        std::string name;
        std::vector<SettingItem> items;
    };

    std::string sizeText(int64_t bytes) {
        if (bytes < 0) {
            return tr("common.unknown");
        }
        return bytes == 0 ? tr("settings.empty") : dl::formatBytes(bytes);
    }

    // one cell of the accent palette: swatch, name, a check mark on the colour in use
    struct PaletteCell {
        RectangleShape *bg;
        CircleShape *swatch;
        ui::Label *name;
        ui::Label *check;
    };

    // one settings row (re-bound to different items while scrolling)
    class SettingRow : public ui::RowFrame {
    public:
        SettingRow(float w, float h) : ui::RowFrame(w, h, 0) {
            caption = ui::label(this, "", theme::BODY, layout::ROW_PAD + 4, ui::Label::centerOffset(theme::BODY, h));
            caption->setMaxWidth(w - layout::ROW_PAD - VALUE_W - 40);
            value = ui::label(this, "", theme::LABEL + 2, 0, ui::Label::centerOffset(theme::LABEL + 2, h),
                              ui::Weight::SemiBold);
            value->setAlign(ui::Align::Right, w - VALUE_RIGHT);
            value->setMaxWidth(VALUE_W);
            for (int i = 0; i < 6; i++) {
                auto *d = new CircleShape(4);
                d->setPointCount(12);
                d->setVisibility(Visibility::Hidden);
                add(d);
                dots.push_back(d);
            }
            float sx = w - VALUE_RIGHT - SWITCH_W;
            track = ui::box(this, FloatRect(sx, (h - SWITCH_H) / 2, SWITCH_W, SWITCH_H), theme::surfaceElevated(),
                            SWITCH_H / 2);
            knob = new CircleShape(SWITCH_H / 2 - 5);
            knob->setPointCount(24);
            track->add(knob);
            switchText = ui::label(this, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h),
                                   ui::Weight::Regular, theme::textSecondary());
            switchText->setAlign(ui::Align::Right, sx - 16);
            // palette cells: three per row, each a third of the row
            const float cw = std::floor((w - (L::PALETTE_COLUMNS - 1) * CELL_GAP) / L::PALETTE_COLUMNS);
            for (int i = 0; i < L::PALETTE_COLUMNS; i++) {
                PaletteCell c;
                c.bg = ui::box(this, FloatRect((float) i * (cw + CELL_GAP), 6, cw, h - 12), theme::none(), theme::RADIUS);
                c.swatch = new CircleShape(SWATCH_R);
                c.swatch->setPointCount(24);
                c.swatch->setPosition(20, (h - 12) / 2 - SWATCH_R);
                c.swatch->setOutlineColor(theme::textStrong());
                c.bg->add(c.swatch);
                c.name = ui::label(c.bg, "", theme::LABEL + 2, 20 + 2 * SWATCH_R + 16,
                                   ui::Label::centerOffset(theme::LABEL + 2, h - 12));
                c.name->setMaxWidth(cw - (20 + 2 * SWATCH_R + 16) - 44);
                c.check = ui::label(c.bg, "\xE2\x9C\x93", theme::BODY, 0, ui::Label::centerOffset(theme::BODY, h - 12),
                                    ui::Weight::SemiBold, theme::accentText());
                c.check->setAlign(ui::Align::Right, cw - 18);
                cells.push_back(c);
            }
        }

        // column: the focused palette cell (Palette rows)
        void bind(const SettingItem &item, bool focused, int column) {
            float w = getSize().x;
            float h = getSize().y;
            bool isPalette = item.kind == Kind::Palette;
            bool isToggle = item.kind == Kind::Toggle;
            setDivider(!isPalette);
            setState(focused && !isPalette);   // palette rows: the focused cell carries the focus
            caption->setVisibility(isPalette ? Visibility::Hidden : Visibility::Visible);
            caption->setText(item.caption);
            caption->setColor(focused ? theme::textStrong() : theme::textPrimary());
            track->setVisibility(isToggle ? Visibility::Visible : Visibility::Hidden);
            switchText->setVisibility(isToggle ? Visibility::Visible : Visibility::Hidden);
            value->setVisibility(isToggle || isPalette ? Visibility::Hidden : Visibility::Visible);
            for (auto *d: dots) {
                d->setVisibility(Visibility::Hidden);
            }
            for (size_t i = 0; i < cells.size(); i++) {
                PaletteCell &c = cells[i];
                bool shown = isPalette && i < item.swatches.size();
                c.bg->setVisibility(shown ? Visibility::Visible : Visibility::Hidden);
                if (!shown) {
                    continue;
                }
                int preset = item.swatches[i];
                bool current = preset == theme::accentIndex();
                bool cellFocused = focused && (int) i == column;
                c.bg->setFillColor(cellFocused ? theme::focus() : theme::none());
                c.bg->setOutlineColor(theme::accent());
                c.bg->setOutlineThickness(cellFocused ? theme::ROW_FOCUS_BORDER : 0);
                // the preset's own colour, a white ring and a check mark on the one in use
                c.swatch->setFillColor(theme::swatch(preset));
                c.swatch->setOutlineThickness(current ? 3 : 0);
                c.name->setText(tr(palette::ACCENTS[preset].nameKey));
                c.name->setWeight(current ? ui::Weight::SemiBold : ui::Weight::Regular);
                c.name->setColor(cellFocused ? theme::textStrong() : current ? theme::textPrimary() : theme::textSecondary());
                c.check->setVisibility(current ? Visibility::Visible : Visibility::Hidden);
            }
            if (isPalette) {
                return;
            }
            if (isToggle) {
                bool on = item.toggle();
                track->setFillColor(on ? theme::accent() : theme::switchOff());
                knob->setFillColor(on ? theme::onAccent() : theme::switchKnobOff());
                float r = SWITCH_H / 2 - 5;
                knob->setPosition(on ? SWITCH_W - 5 - 2 * r : 5, 5);
                switchText->setText(tr(on ? "common.on" : "common.off"));
                switchText->setColor(focused ? theme::textPrimary() : theme::textSecondary());
                return;
            }
            const unsigned vs = theme::LABEL + 2;
            if (item.kind == Kind::Choice) {
                int current = item.choice();
                // position dots only for short choices (language lists are long)
                int n = (int) item.options.size() <= (int) dots.size() ? (int) item.options.size() : 0;
                value->setText(item.options[(size_t) current]);
                value->setColor(focused ? theme::textStrong() : theme::textPrimary());
                value->setPosition(0, ui::Label::centerOffset(vs, h) - (n > 0 ? 7 : 0));
                // option position: small dots under the value, right-aligned with it
                for (int i = 0; i < n; i++) {
                    CircleShape *d = dots[(size_t) i];
                    d->setPosition(w - VALUE_RIGHT - (float) (n - i) * 16 + 8, h - 18);
                    d->setFillColor(i == current ? theme::accent() : theme::withAlpha(theme::textMuted(), 140));
                    d->setVisibility(Visibility::Visible);
                }
                return;
            }
            value->setText(item.info ? item.info() : "");
            value->setColor(focused ? theme::textPrimary() : theme::textMuted());
            value->setPosition(0, ui::Label::centerOffset(vs, h));
        }

    private:
        ui::Label *caption;
        ui::Label *value;
        std::vector<CircleShape *> dots;
        RectangleShape *track;
        CircleShape *knob;
        ui::Label *switchText;
        std::vector<PaletteCell> cells;
    };

    class SettingsScreen : public Screen, public ui::ListView::Adapter {
    public:
        SettingsScreen(App &a, SettingsPage p) : Screen(a), categoriesAdapter(this) {
            ui::background(this);
            screens::header(this, tr("settings.title"));
            buildCategories();
            switch (p) {
                case SettingsPage::Storage:
                    catIndex = CAT_DOWNLOADS;
                    break;
                case SettingsPage::Diagnostics:
                    catIndex = CAT_DIAGNOSTICS;
                    break;
                default:
                    catIndex = CAT_APPEARANCE;
                    break;
            }
            categoryList = new ui::ListView(FloatRect(layout::RAIL_X, L::SECTION_Y, layout::RAIL_W,
                                                      layout::CONTENT_BOTTOM - L::SECTION_Y), L::CAT_ROW_H,
                                            layout::RAIL_ROW_GAP, &categoriesAdapter);
            add(categoryList);
            // the open category as the heading of its settings
            categoryTitle = ui::label(this, "", theme::HEADING - 2, L::LIST_X + layout::ROW_PAD + 4, L::SECTION_Y,
                                      ui::Weight::SemiBold, theme::textPrimary());
            list = new ui::ListView(FloatRect(L::LIST_X, L::LIST_Y, L::LIST_W, L::LIST_H), L::ROW_H, 0, this);
            add(list);
            // the description: plain text after a thin rule, no box
            ui::box(this, FloatRect(L::DIVIDER_X, L::LIST_Y, 1, L::LIST_H), theme::dividerSoft(), 0);
            panelTitle = ui::label(this, "", theme::LABEL + 2, L::PANEL_X, L::LIST_Y, ui::Weight::SemiBold);
            panelTitle->setMaxWidth(L::PANEL_W);
            panelTitle->setMaxLines(3);
            panelText = ui::label(this, "", theme::LABEL, L::PANEL_X, 0, ui::Weight::Regular, theme::textSecondary());
            panelText->setMaxWidth(L::PANEL_W);
            panelText->setMaxLines(20);
            hints = screens::hintBar(this, {});
            categoryList->setSelected(catIndex);
            if (p == SettingsPage::Appearance) {
                // back after an accent change: on the preset that was just applied
                list->setSelected(theme::accentIndex() / L::PALETTE_COLUMNS);
                paletteColumn = theme::accentIndex() % L::PALETTE_COLUMNS;
                setFocus(1);
            } else {
                list->setSelected(0);
                setFocus(p == SettingsPage::Main ? 0 : 1);
            }
            refreshStorage();
            selectCategory(catIndex, false);
        }

        const char *name() const override { return "settings"; }

        void onEnter() override {
            refreshImageCacheSize();
        }

        void onResume() override {
            refreshStorage();
            list->reload();
            refreshPanel();
        }

        void onInputDeviceChanged() override {
            refreshPanel();   // descriptions may name a key
        }

        void tick(double now) override {
            // Storage: sizes follow the downloads (at most once per second)
            if (catIndex == CAT_DOWNLOADS && app.downloads().generation() != downloadsGen && now - lastStorage > 1) {
                lastStorage = now;
                refreshStorage();
                list->reload();
                refreshPanel();
                redraw();
            }
        }

        // ------------------------------------------------------------------ settings list (middle column)
        int count() override { return (int) items().size(); }

        C2DObject *createRow(float w, float h) override {
            return new SettingRow(w, h);
        }

        void bindRow(C2DObject *obj, int index, bool, bool focused) override {
            ((SettingRow *) obj)->bind(items()[(size_t) index], focused, paletteColumn);
        }

        void handleInput(const InputEvent &e) override {
            if (focus == 0) {
                categoryInput(e);
                return;
            }
            SettingItem &item = items()[(size_t) list->selected()];
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    refreshPanel();
                    return;
                case PadButton::Down:
                    list->moveSelection(1);
                    refreshPanel();
                    return;
                case PadButton::Left:
                case PadButton::Right:
                    if (item.kind == Kind::Palette) {
                        // inside the palette; Left on its first column goes back to the categories
                        int col = paletteColumn + (e.button == PadButton::Right ? 1 : -1);
                        if (col < 0) {
                            if (!e.repeat) {
                                setFocus(0);
                            }
                        } else if (col < (int) item.swatches.size()) {
                            paletteColumn = col;
                            list->reload();
                            refreshPanel();
                        }
                        return;
                    }
                    // the D-pad only navigates (ui/option_input.h): Left goes back to the categories, Right does nothing
                    if (ui::optionCommand(e, optionKind(item)) == ui::OptionCommand::Back) {
                        setFocus(0);
                    }
                    return;
                case PadButton::L1:
                case PadButton::R1:
                    if (!e.repeat) {
                        stepCategory(e.button == PadButton::L1 ? -1 : 1);
                    }
                    return;
                case PadButton::Cross:
                    // a value changes only here: 3+ values in the shared picker, On / Off and 2 values in place
                    switch (ui::optionCommand(e, optionKind(item), (int) item.options.size())) {
                        case ui::OptionCommand::OpenPicker:
                            openPicker(list->selected());
                            break;
                        case ui::OptionCommand::Toggle:
                        case ui::OptionCommand::Cycle:
                        case ui::OptionCommand::Activate:
                            change(item, 1, true);
                            break;
                        default:
                            break;
                    }
                    return;
                case PadButton::Circle:
                    if (!e.repeat) {
                        setFocus(0);
                    }
                    return;
                default:
                    return;
            }
        }

    private:
        enum CategoryId {
            CAT_APPEARANCE,
            CAT_PLAYBACK,
            CAT_AUDIO,
            CAT_VIDEO,
            CAT_DOWNLOADS,
            CAT_SOURCES,
            CAT_LANGUAGE,
            CAT_DIAGNOSTICS,
            CAT_ABOUT
        };

        // ------------------------------------------------------------------ categories (left column)
        struct CategoriesAdapter : ui::ListView::Adapter {
            explicit CategoriesAdapter(SettingsScreen *s) : screen(s) {}

            int count() override { return (int) screen->cats.size(); }

            C2DObject *createRow(float w, float h) override {
                Row r;
                r.bg = new ui::RowFrame(w, h, -1);
                r.name = ui::label(r.bg, "", theme::LABEL + 2, layout::ROW_PAD, ui::Label::centerOffset(theme::LABEL + 2, h));
                r.name->setMaxWidth(w - layout::ROW_PAD - 14);
                rows.push_back(r);
                return r.bg;
            }

            void bindRow(C2DObject *obj, int index, bool selected, bool focused) override {
                bool railFocused = screen->focus == 0;
                for (auto &r: rows) {
                    if (r.bg != obj) {
                        continue;
                    }
                    r.name->setText(screen->cats[(size_t) index].name);
                    // the open category keeps its accent edge while its settings have focus
                    r.bg->setState(focused, selected);
                    r.name->setWeight(selected ? ui::Weight::SemiBold : ui::Weight::Regular);
                    r.name->setColor(focused ? theme::textStrong() : selected ? theme::textPrimary()
                                     : railFocused ? theme::textSecondary() : theme::textMuted());
                }
            }

            struct Row {
                ui::RowFrame *bg;
                ui::Label *name;
            };
            SettingsScreen *screen;
            std::vector<Row> rows;
        };

        void categoryInput(const InputEvent &e) {
            switch (e.button) {
                case PadButton::Up:
                case PadButton::Down:
                    if (categoryList->moveSelection(e.button == PadButton::Up ? -1 : 1)) {
                        selectCategory(categoryList->selected(), true);
                    }
                    return;
                case PadButton::L1:
                case PadButton::R1:
                    if (!e.repeat) {
                        stepCategory(e.button == PadButton::L1 ? -1 : 1);
                    }
                    return;
                case PadButton::Right:
                case PadButton::Cross:
                    if (!e.repeat && !items().empty()) {
                        setFocus(1);
                    }
                    return;
                case PadButton::Circle:
                    if (!e.repeat) {
                        app.pop();
                    }
                    return;
                default:
                    return;
            }
        }

        void stepCategory(int d) {
            int target = std::min(std::max(catIndex + d, 0), (int) cats.size() - 1);
            if (target != catIndex) {
                categoryList->setSelected(target);
                selectCategory(target, true);
            }
        }

        void selectCategory(int index, bool resetSelection) {
            catIndex = index;
            if (resetSelection) {
                list->setSelected(0);
            }
            list->reload();
            categoryTitle->setText(cats[(size_t) catIndex].name);
            refreshPanel();
        }

        void setFocus(int f) {
            focus = f;
            categoryList->setFocused(focus == 0);
            list->setFocused(focus == 1);
            categoryList->reload();   // the other categories step back while the settings have focus
            if (focus == 0) {
                hints->setHints({{A::Confirm, tr("common.open")},
                                 {A::Tabs, tr("live.category")}, {A::Back, tr("common.back")}});
            } else {
                bool swatches = catIndex == CAT_APPEARANCE;
                // the D-pad only navigates: Cross changes a value (Change is shown only for the palette's swatches)
                screens::Hints h = {{A::Confirm, tr(swatches ? "settings.hint_apply" : "common.select")}};
                if (swatches) {
                    h.push_back({A::Change, tr("settings.hint_change")});
                }
                h.push_back({A::Tabs, tr("live.category")});
                h.push_back({A::Back, tr("settings.hint_categories")});
                hints->setHints(h);
            }
            refreshPanel();
        }

        bool playbackRebuild = false;   // Auto-play changed: Playback's rows are built again after the toggle

        std::vector<SettingItem> &items() { return cats[(size_t) catIndex].items; }

        // Playback's rows depend on Auto-play (the card timing row): built again, the selection kept on its row
        void rebuildPlayback() {
            playbackRebuild = false;
            int at = list->selected();
            cats[CAT_PLAYBACK].items.clear();
            buildPlayback(cats[CAT_PLAYBACK].items);
            if (catIndex == CAT_PLAYBACK) {
                list->reload();
                list->setSelected(std::min(at, (int) items().size() - 1));
                refreshPanel();
            }
        }

        static ui::OptionKind optionKind(const SettingItem &item) {
            return item.kind == Kind::Toggle ? ui::OptionKind::Toggle : item.kind == Kind::Choice ? ui::OptionKind::Choice
                                                                                                 : ui::OptionKind::Action;
        }

        // the shared choice picker for a Choice row; the value applies when it closes (the row shows it at once)
        void openPicker(int row) {
            const SettingItem &item = items()[(size_t) row];
            int category = catIndex;
            app.push(screens::makeChoicePicker(app, item.caption, item.options, item.choice(), whileAlive([this, category, row](int c) {
                if (c < 0 || category != catIndex || row >= (int) items().size()) {
                    return;   // cancelled (or the page changed meanwhile)
                }
                SettingItem &it = items()[(size_t) row];
                if (it.kind != Kind::Choice || c == it.choice()) {
                    return;
                }
                it.setChoice(c);   // may rebuild every screen (language): nothing of this screen is used after it
            })));
        }

        void change(SettingItem &item, int delta, bool activate) {
            if (item.kind == Kind::Choice) {
                int n = (int) item.options.size();
                int next = item.choice() + delta;
                if (activate) {
                    next = (next + n) % n;      // X cycles
                } else if (next < 0 || next >= n) {
                    return;                     // Left/Right stop at the ends
                }
                item.setChoice(next);
            } else if (item.kind == Kind::Toggle) {
                bool on = item.toggle();
                bool want = activate ? !on : delta > 0;
                if (want == on) {
                    return;
                }
                item.setToggle(want);
                if (playbackRebuild) {
                    rebuildPlayback();   // `item` belongs to the old list: nothing of it is used after this
                    return;
                }
            } else if (activate && item.kind == Kind::Palette) {
                if (paletteColumn < (int) item.swatches.size()) {
                    applyAccent(item.swatches[(size_t) paletteColumn]);
                }
                return;   // the screens are rebuilt with the new accent: this screen is on its way out
            } else if (activate && item.run) {
                item.run();
                return;   // an action may have rebuilt the screens (accent): this screen may be on its way out
            } else {
                return;
            }
            list->reload();
            refreshPanel();
        }

        // saves the preset and rebuilds every screen with it (this one included: nothing of it is used after)
        void applyAccent(int preset) {
            if (preset == theme::accentIndex()) {
                return;
            }
            app.settings().get().accentColor = palette::ACCENTS[preset].key;
            save();
            App &a = app;
            std::string name = tr(palette::ACCENTS[preset].nameKey);
            a.applyAccent(true);   // back here, on Appearance, with the new accent
            a.toast(tr("appearance.accent.applied", {name}), ToastKind::Success);
        }

        void refreshPanel() {
            if (items().empty()) {
                return;
            }
            const SettingItem &item = focus == 0 ? items()[0] : items()[(size_t) list->selected()];
            panelTitle->setText(focus == 0 ? cats[(size_t) catIndex].name : item.caption);
            std::string text = focus == 0 && catIndex == CAT_APPEARANCE ? tr("settings.cat.appearance.desc")
                               : item.describe ? item.describe() : "";
            panelText->setText(text);
            panelText->setPosition(L::PANEL_X, L::LIST_Y + panelTitle->height() + 14);
        }

        void save() {
            std::string err;
            if (!app.settings().save(&err)) {
                LOG_E("settings", "save failed: %s", err.c_str());
                app.toast(tr("settings.save_failed"), ToastKind::Error);
            }
        }

        void refreshImageCacheSize() {
            app.images().refreshDiskUsage(guarded([this] {
                list->reload();
                redraw();
            }));
        }

        void refreshStorage() {
            downloadsGen = app.downloads().generation();
            totals = app.downloads().totals();
            freeBytes = app.downloads().freeBytes();
        }

        // "" (Auto) + common languages + languages met in played files + the saved choices
        std::vector<std::string> languageCodes() const {
            std::vector<std::string> codes = {""};
            auto addCode = [&codes](const std::string &c) {
                std::string n = tracks::normalizeLanguage(c);
                if (!n.empty() && std::find(codes.begin(), codes.end(), n) == codes.end()) {
                    codes.push_back(n);
                }
            };
            for (const auto &c: tracks::commonLanguages()) {
                addCode(c);
            }
            for (const auto &c: app.session().seenLanguages) {
                addCode(c);
            }
            addCode(app.settings().get().audioLanguage);
            addCode(app.settings().get().subtitleLanguage);
            return codes;
        }

        void confirm(const std::string &title, const std::string &message, const std::string &button,
                     std::function<void()> action) {
            app.push(screens::makeDialog(app, title, message, {tr("common.cancel"), button}, [action](int c) {
                if (c == 1) {
                    action();
                }
            }, true));
        }

        // every category, in CategoryId order; `items` is filled by each builder
        void buildCategories() {
            const char *names[] = {"appearance.title", "settings.cat.playback", "settings.cat.audio_subtitles",
                                   "settings.cat.video", "settings.storage_title", "profiles.title", "settings.language",
                                   "settings.diagnostics_title", "settings.cat.about"};
            for (const char *n: names) {
                cats.push_back({tr(n), {}});
            }
            buildAppearance(cats[CAT_APPEARANCE].items);
            buildPlayback(cats[CAT_PLAYBACK].items);
            buildAudio(cats[CAT_AUDIO].items);
            buildVideo(cats[CAT_VIDEO].items);
            buildDownloads(cats[CAT_DOWNLOADS].items);
            buildStorage(cats[CAT_DOWNLOADS].items);
            buildSources(cats[CAT_SOURCES].items);
            buildLanguage(cats[CAT_LANGUAGE].items);
            buildDiagnostics(cats[CAT_DIAGNOSTICS].items);
            buildAbout(cats[CAT_ABOUT].items);
        }

        // ------------------------------------------------------------------ Appearance
        void buildAppearance(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            (void) s;
            // the presets as rows of three: V7 Blue Cyan Teal / Green Purple Magenta / Red Orange White
            for (int i = 0; i < palette::ACCENT_COUNT; i += L::PALETTE_COLUMNS) {
                SettingItem row;
                row.kind = Kind::Palette;
                row.caption = tr("appearance.accent");
                for (int j = i; j < std::min(palette::ACCENT_COUNT, i + L::PALETTE_COLUMNS); j++) {
                    row.swatches.push_back(j);
                }
                row.describe = [] {
                    return tr("appearance.accent.desc") + "\n\n"
                           + tr("appearance.accent.current", {tr(theme::accentPreset().nameKey),
                                                               screens::keyName(A::Confirm)});
                };
                items.push_back(row);
            }
            SettingItem smooth;
            smooth.kind = Kind::Toggle;
            smooth.caption = tr("settings.smooth_scrolling");
            smooth.toggle = [&s] { return s.smoothScrolling; };
            smooth.setToggle = [this, &s](bool on) {
                s.smoothScrolling = on;
                ui::setSmoothScrolling(on);
                save();
            };
            smooth.describe = [] { return tr("settings.smooth_scrolling.desc"); };
            items.push_back(smooth);

            SettingItem images;
            images.kind = Kind::Toggle;
            images.caption = tr("settings.images");
            images.toggle = [&s] { return s.loadImages; };
            images.setToggle = [this, &s](bool on) {
                s.loadImages = on;
                app.images().setEnabled(on);
                save();
            };
            images.describe = [] { return tr("settings.images.desc"); };
            items.push_back(images);
        }

        // ------------------------------------------------------------------ Playback
        void buildPlayback(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            SettingItem format;
            format.kind = Kind::Choice;
            format.caption = tr("settings.stream_format");
            format.options = {tr("settings.stream_format.auto"), tr("settings.stream_format.ts"),
                              tr("settings.stream_format.hls")};
            format.choice = [&s] { return s.streamFormat == StreamFormat::Ts ? 1 : s.streamFormat == StreamFormat::Hls ? 2 : 0; };
            format.setChoice = [this, &s](int i) {
                s.streamFormat = i == 1 ? StreamFormat::Ts : i == 2 ? StreamFormat::Hls : StreamFormat::Auto;
                save();
            };
            format.describe = [&s] {
                return tr(s.streamFormat == StreamFormat::Ts ? "settings.stream_format.ts_desc"
                          : s.streamFormat == StreamFormat::Hls ? "settings.stream_format.hls_desc"
                                                                : "settings.stream_format.auto_desc");
            };
            items.push_back(format);

            SettingItem stab;
            stab.kind = Kind::Choice;
            stab.caption = tr("settings.stability");
            stab.options = {tr("stability.fast"), tr("stability.balanced"), tr("stability.max")};
            stab.choice = [&s] { return s.stability == StabilityPreset::Fast ? 0 : s.stability == StabilityPreset::MaxStability ? 2 : 1; };
            stab.setChoice = [this, &s](int i) {
                s.stability = i == 0 ? StabilityPreset::Fast : i == 2 ? StabilityPreset::MaxStability
                                                                      : StabilityPreset::Balanced;
                save();
            };
            stab.describe = [&s] {
                return tr(s.stability == StabilityPreset::Fast ? "settings.stability.fast_desc"
                          : s.stability == StabilityPreset::MaxStability ? "settings.stability.max_desc"
                                                                         : "settings.stability.balanced_desc");
            };
            items.push_back(stab);

            SettingItem retry;
            retry.kind = Kind::Toggle;
            retry.caption = tr("settings.retry_stall");
            retry.toggle = [&s] { return s.retryOnStall; };
            retry.setToggle = [this, &s](bool on) {
                s.retryOnStall = on;
                save();
            };
            retry.describe = [&s] { return tr(s.retryOnStall ? "settings.retry_stall.on_desc" : "settings.retry_stall.off_desc"); };
            items.push_back(retry);

            SettingItem resume;
            resume.kind = Kind::Toggle;
            resume.caption = tr("settings.resume");
            resume.toggle = [&s] { return s.resumeVod; };
            resume.setToggle = [this, &s](bool on) {
                s.resumeVod = on;
                save();
            };
            resume.describe = [] { return tr("settings.resume.desc"); };
            items.push_back(resume);

            SettingItem autoNext;
            autoNext.kind = Kind::Toggle;
            autoNext.caption = tr("settings.autoplay");
            autoNext.toggle = [&s] { return s.autoPlayNextEpisode; };
            autoNext.setToggle = [this, &s](bool on) {
                s.autoPlayNextEpisode = on;
                save();
                playbackRebuild = true;   // the card timing row appears / disappears (after this call: change())
            };
            autoNext.describe = [] { return tr("settings.autoplay.desc"); };
            items.push_back(autoNext);

            // when the Next Episode card appears (only with Auto-play on); its 7 s countdown never changes
            if (s.autoPlayNextEpisode) {
                SettingItem cardTime;
                cardTime.kind = Kind::Choice;
                cardTime.caption = tr("settings.next_card_time");
                for (int v: nextCardChoices()) {
                    cardTime.options.push_back(i18n::count("settings.next_card_seconds", (long long) v));
                }
                cardTime.choice = [&s] {
                    const std::vector<int> &c = nextCardChoices();
                    int v = clampNextCardSeconds(s.nextCardSeconds);
                    for (size_t i = 0; i < c.size(); i++) {
                        if (c[i] == v) {
                            return (int) i;
                        }
                    }
                    return 0;
                };
                cardTime.setChoice = [this, &s](int i) {
                    const std::vector<int> &c = nextCardChoices();
                    s.nextCardSeconds = c[(size_t) std::min(std::max(i, 0), (int) c.size() - 1)];
                    save();
                };
                cardTime.describe = [] { return tr("settings.next_card_time.desc"); };
                items.push_back(cardTime);
            }

            SettingItem tech;
            tech.kind = Kind::Toggle;
            tech.caption = tr("settings.tech_info");
            tech.toggle = [&s] { return s.showTechnicalInfo; };
            tech.setToggle = [this, &s](bool on) {
                s.showTechnicalInfo = on;
                save();
            };
            tech.describe = [] { return tr("settings.tech_info.desc"); };
            items.push_back(tech);
        }

        // ------------------------------------------------------------------ Audio & subtitles (movies / episodes)
        void buildAudio(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            std::vector<std::string> codes = languageCodes();
            std::vector<std::string> names;
            for (const auto &c: codes) {
                names.push_back(c.empty() ? "" : tracks::languageName(c));
            }
            auto indexOfCode = [codes](const std::string &code) {
                for (size_t i = 0; i < codes.size(); i++) {
                    if (codes[i] == code) {
                        return (int) i;
                    }
                }
                return 0;
            };

            SettingItem audio;
            audio.kind = Kind::Choice;
            audio.caption = tr("settings.audio_language");
            audio.options = names;
            audio.options[0] = tr("settings.audio_language.auto");
            audio.choice = [&s, indexOfCode] { return indexOfCode(s.audioLanguage); };
            audio.setChoice = [this, &s, codes](int i) {
                s.audioLanguage = codes[(size_t) i];
                save();
            };
            audio.describe = [] { return tr("settings.audio_language.desc"); };
            items.push_back(audio);

            SettingItem subMode;
            subMode.kind = Kind::Choice;
            subMode.caption = tr("settings.subtitles");
            subMode.options = {tr("common.off"), tr("settings.subtitles.auto"), tr("settings.subtitles.always")};
            subMode.choice = [&s] { return (int) s.subtitleMode; };
            subMode.setChoice = [this, &s](int i) {
                s.subtitleMode = (tracks::SubtitleMode) i;
                save();
            };
            subMode.describe = [&s] {
                return tr(s.subtitleMode == tracks::SubtitleMode::Off ? "settings.subtitles.off_desc"
                          : s.subtitleMode == tracks::SubtitleMode::Always ? "settings.subtitles.always_desc"
                                                                           : "settings.subtitles.auto_desc");
            };
            items.push_back(subMode);

            SettingItem subLang;
            subLang.kind = Kind::Choice;
            subLang.caption = tr("settings.subtitle_language");
            subLang.options = names;
            subLang.options[0] = tr("settings.subtitle_language.same");
            subLang.choice = [&s, indexOfCode] { return indexOfCode(s.subtitleLanguage); };
            subLang.setChoice = [this, &s, codes](int i) {
                s.subtitleLanguage = codes[(size_t) i];
                save();
            };
            subLang.describe = [] { return tr("settings.subtitle_language.desc"); };
            items.push_back(subLang);

            SettingItem subSize;
            subSize.kind = Kind::Choice;
            subSize.caption = tr("subtitle.size");
            subSize.options = {tr("subtitle.size.small"), tr("subtitle.size.medium"), tr("subtitle.size.large")};
            subSize.choice = [&s] { return std::min(std::max(s.subtitleSize, 0), 2); };
            subSize.setChoice = [this, &s](int i) {
                s.subtitleSize = i;
                save();
            };
            subSize.describe = [] { return tr("settings.subtitle_size.desc"); };
            items.push_back(subSize);

            SettingItem subPos;
            subPos.kind = Kind::Choice;
            subPos.caption = tr("subtitle.position");
            subPos.options = {tr("subtitle.position.bottom"), tr("subtitle.position.raised")};
            subPos.choice = [&s] { return s.subtitlePosition == 1 ? 1 : 0; };
            subPos.setChoice = [this, &s](int i) {
                s.subtitlePosition = i;
                save();
            };
            subPos.describe = [] { return tr("settings.subtitle_position.desc"); };
            items.push_back(subPos);

            SettingItem subShadow;
            subShadow.kind = Kind::Toggle;
            subShadow.caption = tr("subtitle.shadow");
            subShadow.toggle = [&s] { return s.subtitleShadow; };
            subShadow.setToggle = [this, &s](bool on) {
                s.subtitleShadow = on;
                save();
            };
            subShadow.describe = [] { return tr("settings.subtitle_shadow.desc"); };
            items.push_back(subShadow);
        }

        // ------------------------------------------------------------------ Video (defaults; the Options panel changes only the current playback)
        void buildVideo(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            SettingItem aspect;
            aspect.kind = Kind::Choice;
            aspect.caption = tr("settings.aspect");
            for (int i = 0; i < display::ASPECT_COUNT; i++) {
                aspect.options.push_back(screens::aspectName((display::Aspect) i));
            }
            aspect.choice = [&s] { return (int) s.videoAspect; };
            aspect.setChoice = [this, &s](int i) {
                s.videoAspect = (display::Aspect) i;
                save();
            };
            aspect.describe = [&s] { return tr("settings.aspect.desc", {screens::aspectName(s.videoAspect)}); };
            items.push_back(aspect);

            SettingItem crop;
            crop.kind = Kind::Choice;
            crop.caption = tr("settings.crop");
            for (int i = 0; i < display::CROP_COUNT; i++) {
                crop.options.push_back(screens::cropName((display::Crop) i));
            }
            crop.choice = [&s] { return (int) s.videoCrop; };
            crop.setChoice = [this, &s](int i) {
                s.videoCrop = (display::Crop) i;
                save();
            };
            crop.describe = [&s] { return tr("settings.crop.desc", {screens::cropName(s.videoCrop)}); };
            items.push_back(crop);

            SettingItem zoom;
            zoom.kind = Kind::Choice;
            zoom.caption = tr("settings.zoom");
            for (int z: display::zoomSteps()) {
                zoom.options.push_back(screens::zoomName(z));
            }
            zoom.choice = [&s] {
                const auto &steps = display::zoomSteps();
                for (size_t i = 0; i < steps.size(); i++) {
                    if (steps[i] == display::clampZoom(s.zoomPercent)) {
                        return (int) i;
                    }
                }
                return 0;
            };
            zoom.setChoice = [this, &s](int i) {
                s.zoomPercent = display::zoomSteps()[(size_t) i];
                save();
            };
            zoom.describe = [] { return tr("settings.zoom.desc"); };
            items.push_back(zoom);
        }

        // ------------------------------------------------------------------ Downloads & storage
        void buildDownloads(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            SettingItem retryDl;
            retryDl.kind = Kind::Toggle;
            retryDl.caption = tr("settings.retry_downloads");
            retryDl.toggle = [&s] { return s.retryDownloads; };
            retryDl.setToggle = [this, &s](bool on) {
                s.retryDownloads = on;
                app.downloads().setAutoRetry(on);
                save();
            };
            retryDl.describe = [] { return tr("settings.retry_downloads.desc"); };
            items.push_back(retryDl);

            SettingItem resumeDl;
            resumeDl.kind = Kind::Toggle;
            resumeDl.caption = tr("settings.resume_downloads");
            resumeDl.toggle = [&s] { return s.resumeDownloadsOnStart; };
            resumeDl.setToggle = [this, &s](bool on) {
                s.resumeDownloadsOnStart = on;
                save();
            };
            resumeDl.describe = [] { return tr("settings.resume_downloads.desc"); };
            items.push_back(resumeDl);
        }

        void buildStorage(std::vector<SettingItem> &items) {
            SettingItem downloaded;
            downloaded.caption = tr("storage.downloaded");
            downloaded.info = [this] { return sizeText(totals.completedBytes); };
            downloaded.run = [this] { app.push(screens::makeDownloads(app)); };
            downloaded.describe = [this] {
                return tr("storage.downloaded.desc", {i18n::count("storage.items", totals.completed)});
            };
            items.push_back(downloaded);

            SettingItem partial;
            partial.caption = tr("storage.partial");
            partial.info = [this] { return sizeText(totals.partialBytes); };
            partial.run = [this] {
                if (totals.active == 0) {
                    app.toast(tr("storage.partial_none"));
                    return;
                }
                confirm(tr("storage.delete_partial_title"), tr("storage.delete_partial_text"), tr("common.delete"), [this] {
                    int n = app.downloads().removePartials();
                    app.toast(i18n::count("storage.deleted_count", n), ToastKind::Success);
                    refreshStorage();
                    list->reload();
                });
            };
            partial.describe = [] { return tr("storage.partial.desc"); };
            items.push_back(partial);

            SettingItem free;
            free.caption = tr("storage.available");
            free.info = [this] { return sizeText(freeBytes); };
            free.run = [this] {
                refreshStorage();
                list->reload();
            };
            free.describe = [] { return tr("storage.available.desc"); };
            items.push_back(free);

            SettingItem manage;
            manage.caption = tr("storage.manage");
            manage.info = [] { return tr("settings.open"); };
            manage.run = [this] { app.push(screens::makeDownloads(app)); };
            manage.describe = [] { return tr("storage.manage.desc"); };
            items.push_back(manage);

            SettingItem deleteAll;
            deleteAll.caption = tr("storage.delete_completed");
            deleteAll.info = [this] { return i18n::count("storage.items", totals.completed); };
            deleteAll.run = [this] {
                if (totals.completed == 0) {
                    app.toast(tr("storage.completed_none"));
                    return;
                }
                confirm(tr("storage.delete_completed_title"),
                        tr("storage.delete_completed_text", {i18n::count("storage.items", totals.completed),
                                                             dl::formatBytes(totals.completedBytes)}),
                        tr("common.delete"), [this] {
                            int n = app.downloads().removeAllCompleted();
                            app.toast(i18n::count("storage.deleted_count", n), ToastKind::Success);
                            refreshStorage();
                            list->reload();
                        });
            };
            deleteAll.describe = [] { return tr("storage.delete_completed.desc"); };
            items.push_back(deleteAll);

            SettingItem clearImages;
            clearImages.caption = tr("storage.clear_images");
            clearImages.info = [this] { return sizeText(app.images().diskBytes()); };
            clearImages.run = [this] {
                confirm(tr("storage.clear_images_title"), tr("storage.clear_images_text"), tr("common.clear"), [this] {
                    std::function<void()> done = guarded([this] {
                        app.toast(tr("storage.images_cleared"), ToastKind::Success);
                        list->reload();
                    });
                    app.images().clearCache([done](int64_t) { done(); });
                });
            };
            clearImages.describe = [] { return tr("storage.clear_images.desc"); };
            items.push_back(clearImages);

            SettingItem clearMeta;
            clearMeta.caption = tr("storage.clear_metadata");
            clearMeta.run = [this] {
                confirm(tr("storage.clear_metadata_title"), tr("storage.clear_metadata_text"), tr("common.clear"), [this] {
                    app.jobs().submit(JobPriority::High, "clear-metadata", [](const CancelToken &) {
                        CatalogCache(APP_DATA_DIR).clearAll();
                    }, guarded([this] {
                        LOG_I("settings", "metadata cache cleared");
                        app.toast(tr("storage.metadata_cleared"), ToastKind::Success);
                    }));
                });
            };
            clearMeta.describe = [] { return tr("storage.clear_metadata.desc"); };
            items.push_back(clearMeta);

            SettingItem clearHistory;
            clearHistory.caption = tr("storage.clear_history");
            clearHistory.info = [this] {
                size_t n = app.library().history().size();
                return n == 0 ? tr("settings.empty") : i18n::count("storage.entries", (long long) n);
            };
            clearHistory.run = [this] {
                if (app.library().history().empty()) {
                    app.toast(tr("storage.history_empty"));
                    return;
                }
                confirm(tr("storage.clear_history_title"), tr("storage.clear_history_text"), tr("common.clear"), [this] {
                    app.library().clearHistory();
                    app.saveLibrary();
                    app.toast(tr("storage.history_cleared"), ToastKind::Success);
                    list->reload();
                });
            };
            clearHistory.describe = [] { return tr("storage.clear_history.desc"); };
            items.push_back(clearHistory);
        }

        // ------------------------------------------------------------------ Sources
        void buildSources(std::vector<SettingItem> &items) {
            SettingItem profiles;
            profiles.caption = tr("profiles.title");
            profiles.info = [] { return tr("settings.manage"); };
            profiles.run = [this] { app.push(screens::makeProfiles(app)); };
            profiles.describe = [this] {
                const Session &s = app.session();
                return tr("settings.profiles.desc")
                       + (s.profile.name.empty() ? "" : "\n\n" + tr("settings.sources.active", {s.profile.name}));
            };
            items.push_back(profiles);
        }

        // ------------------------------------------------------------------ Language
        void buildLanguage(std::vector<SettingItem> &items) {
            Settings &s = app.settings().get();
            SettingItem language;
            language.kind = Kind::Choice;
            language.caption = tr("settings.language");
            // each language in its own name: whoever picked the wrong one can still find theirs
            language.options = {i18n::nativeName(i18n::Language::English), i18n::nativeName(i18n::Language::Turkish)};
            language.choice = [&s] { return s.language == "tr" ? 1 : 0; };
            language.setChoice = [this, &s](int i) {
                std::string code = i == 1 ? "tr" : "en";
                if (code == s.language) {
                    return;
                }
                s.language = code;
                save();
                app.applyLanguage(true);   // rebuilds every screen in the new language
            };
            language.describe = [] { return tr("settings.language.desc"); };
            items.push_back(language);

            // a physical USB / Bluetooth keyboard: which characters its keys type
            SettingItem layout;
            layout.kind = Kind::Choice;
            layout.caption = tr("settings.keyboard_layout");
            layout.options = {tr("settings.keyboard_layout.auto"), tr("settings.keyboard_layout.us"),
                              tr("settings.keyboard_layout.tr")};
            layout.choice = [&s] { return s.keyboardLayout == "us" ? 1 : s.keyboardLayout == "tr" ? 2 : 0; };   // i18n-exempt: technical value
            layout.setChoice = [this, &s](int i) {
                s.keyboardLayout = i == 1 ? "us" : i == 2 ? "tr" : "auto";   // i18n-exempt: technical value
                save();
                app.applyKeyboardLayout();
            };
            layout.describe = [] { return tr("settings.keyboard_layout.desc"); };
            items.push_back(layout);
        }

        void buildDiagnostics(std::vector<SettingItem> &items) {
            SettingItem downloads;
            downloads.caption = tr("diagnostics.downloads");
            downloads.info = [] { return tr("settings.open"); };
            downloads.run = [this] { app.push(screens::makeDownloadDiagnostics(app)); };
            downloads.describe = [] { return tr("diagnostics.downloads.desc"); };
            items.push_back(downloads);

            SettingItem textTest;
            textTest.caption = tr("diagnostics.text_test");
            textTest.info = [] { return tr("settings.open"); };
            textTest.run = [this] { app.push(screens::makeTextTest(app, false)); };
            textTest.describe = [] { return tr("diagnostics.text_test.desc"); };
            items.push_back(textTest);

            SettingItem build;
            build.caption = tr("diagnostics.build");
            build.info = [] { return std::string(BUILD_GIT_HASH); };
            build.describe = [] {
                return tr("diagnostics.build.desc", {APP_VERSION, BUILD_GIT_HASH, BUILD_DATE, BUILD_TYPE});
            };
            items.push_back(build);
        }

        // ------------------------------------------------------------------ About
        void buildAbout(std::vector<SettingItem> &items) {
            SettingItem about;
            about.caption = tr("about.title");
            about.info = [] { return std::string("v") + APP_VERSION; };
            about.run = [this] { app.push(screens::makeAbout(app)); };
            about.describe = [] { return tr("settings.about.desc"); };
            items.push_back(about);
        }

        CategoriesAdapter categoriesAdapter;
        std::vector<Category> cats;
        int catIndex = 0;
        int paletteColumn = 0;   // the focused cell of a palette row
        int focus = 0;
        ui::ListView *categoryList = nullptr;
        ui::ListView *list = nullptr;
        ui::Label *categoryTitle = nullptr;
        ui::Label *panelTitle = nullptr;
        ui::Label *panelText = nullptr;
        ui::HintBar *hints = nullptr;
        dl::Totals totals;
        int64_t freeBytes = -1;
        unsigned downloadsGen = 0;
        double lastStorage = 0;
    };

    class AboutScreen : public Screen {
    public:
        explicit AboutScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, tr("about.title"), tr("about.version", {APP_VERSION, BUILD_DATE, BUILD_GIT_HASH}));
            // the project names and licences are proper names, identical in every language
            // i18n-exempt-begin
            std::string text = tr("about.license") + "\n" + tr("about.no_content") + "\n\n" + tr("about.built_on") + "\n"
                               "pPlay (Cpasjuste) - GPL-3.0\n"
                               "libcross2d (Cpasjuste) - GPL-3.0\n"
                               "mpv 0.34.1 + PS4 patches (PacBrew) - GPL-2.0-or-later\n"
                               "FFmpeg 5.0 - LGPL-2.1-or-later / GPL\n"
                               "SDL 2.0.18 PS4 port (PacBrew) - zlib\n"
                               "libass, FreeType, FriBidi, libpng, zlib, bzip2, Opus - ISC / FTL / LGPL / BSD / zlib\n"
                               "stb_image (Sean Barrett, via libcross2d) - public domain / MIT\n"
                               "libcurl 7.80 (curl license), Mbed TLS 2.16 (Apache-2.0)\n"
                               "OpenOrbis PS4 Toolchain (GPL-3.0), PacBrew musl (MIT), libc++ (Apache-2.0 WITH LLVM-exception)\n"
                               "Inter (Rasmus Andersson) - SIL Open Font License 1.1\n"
                               "DejaVu Sans (Bitstream, DejaVu authors) - Bitstream Vera license / public domain\n"
                               "Droid Sans Fallback (Android Open Source Project) - Apache-2.0\n"
                               "Mozilla CA certificate bundle - MPL-2.0\n\n" + tr("about.notices");
            // i18n-exempt-end
            auto *body = ui::label(this, text, theme::LABEL, theme::SAFE_X, 210, ui::Weight::Regular, theme::textSecondary());
            body->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X);
            body->setMaxLines(24);
            screens::hintBar(this, {{A::Back, tr("common.back")}});
        }

        const char *name() const override { return "about"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Circle || e.button == PadButton::Cross) {
                app.pop();
            }
        }
    };

    // Settings > Diagnostics > Download diagnostics: the download engine's measurements, refreshed 4 times a
    // second. 1 MB = 1,000,000 bytes here (comparable with the PC control measurement).
    class DownloadDiagnosticsScreen : public Screen {
    public:
        explicit DownloadDiagnosticsScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, tr("dldiag.title"), tr("dldiag.subtitle"));
            heading = ui::label(this, "", theme::HEADING, theme::SAFE_X, 210, ui::Weight::SemiBold);
            heading->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X);
            names = ui::label(this, "", theme::LABEL, theme::SAFE_X, 290, ui::Weight::Regular, theme::textSecondary());
            names->setMaxLines(MAX_LINES);
            names->setMaxWidth(NAME_W - 40);
            values = ui::label(this, "", theme::LABEL, theme::SAFE_X + NAME_W, 290, ui::Weight::SemiBold);
            values->setMaxLines(MAX_LINES);
            values->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X - NAME_W);
            screens::hintBar(this, {{A::Back, tr("common.back")}});
            refreshStats();
        }

        const char *name() const override { return "download-diagnostics"; }

        void tick(double now) override {
            if (now - lastUpdate >= 0.25) {
                lastUpdate = now;
                refreshStats();
            }
        }

        void handleInput(const InputEvent &e) override {
            if (!e.repeat && (e.button == PadButton::Circle || e.button == PadButton::Cross)) {
                app.pop();
            }
        }

    private:
        static const int MAX_LINES = 20;
        static constexpr float NAME_W = 620;

        // units (MB/s, Mbit/s, MB, KB, s) are the same in every language
        // i18n-exempt-begin
        // "2.45 MB/s (19.6 Mbit/s)" with the language's decimal point
        static std::string rate(double bytesPerSecond) {
            if (bytesPerSecond <= 0) {
                return "-";
            }
            return decimal(bytesPerSecond / 1e6, 2) + " MB/s  (" + decimal(bytesPerSecond * 8 / 1e6, 1) + " Mbit/s)";
        }

        static std::string decimal(double v, int places) {
            char buf[48];
            snprintf(buf, sizeof(buf), "%.*f", places, v);
            std::string s = buf;
            size_t dot = s.find('.');
            if (dot != std::string::npos) {
                s.replace(dot, 1, tr("format.decimal_point"));
            }
            return s;
        }

        static std::string megabytes(double bytes) {
            return decimal(bytes / 1e6, 1) + " MB";
        }

        static std::string kilobytes(double bytes) {
            return bytes < 0 ? tr("common.unknown") : decimal(bytes / 1024, bytes < 10240 ? 1 : 0) + " KB";
        }

        static std::string seconds(double s) {
            return s < 0 ? "-" : decimal(s, s < 10 ? 2 : 1) + " s";
        }
        // i18n-exempt-end

        void refreshStats() {
            dl::TransferStats s = app.downloads().transferStats();
            std::string text, vals;
            if (s.title.empty()) {
                heading->setText(tr("dldiag.none"));
            } else {
                heading->setText(tr(s.active ? "dldiag.active" : "dldiag.last", {s.title}));
                std::string resumed = s.startOffset > 0
                                      ? "  (" + tr("dldiag.from", {megabytes((double) s.startOffset)}) + ")" : "";
                std::string callbackRange = kilobytes((double) s.minCallback) + " / "
                                            + kilobytes((double) s.maxCallback);
                std::string flushes = tr("dldiag.flushes_value", {std::to_string(s.syncs), seconds(s.syncSeconds)});
                std::vector<std::pair<const char *, std::string>> rows = {
                        {"dldiag.current", rate(s.smoothed)},
                        {"dldiag.average", rate(s.average())},
                        {"dldiag.peak", rate(s.peak)},
                        {"dldiag.network", rate(s.networkSpeed())},
                        {"dldiag.disk", rate(s.diskSpeed())},
                        {"dldiag.transferred", megabytes((double) s.bytes) + resumed},
                        {"dldiag.elapsed", seconds(s.elapsed)},
                        {"dldiag.first_byte", seconds(s.firstByte)},
                        {"dldiag.callbacks", tr("dldiag.callbacks_value", {std::to_string(s.callbacks),
                                                                           kilobytes(s.averageCallback()),
                                                                           decimal(s.callbacksPerSecond(), 0)})},
                        {"dldiag.callback_range", callbackRange},
                        {"dldiag.flushes", flushes},
                        {"dldiag.manifest", std::to_string(s.manifestWrites)},
                        {"dldiag.http", s.httpStatus > 0 ? std::to_string(s.httpStatus) : "-"},
                        {"dldiag.length", tr(s.lengthKnown ? "common.yes" : "common.no")},
                        {"dldiag.range", s.rangeSupport < 0 ? tr("common.unknown")
                                                            : tr(s.rangeSupport > 0 ? "common.yes" : "common.no")},
                        {"dldiag.rcvbuf", tr("dldiag.rcvbuf_value", {kilobytes(s.receiveBufferDefault),
                                                                     kilobytes(s.receiveBuffer)})},
                        {"dldiag.curlbuf", kilobytes((double) s.transferBuffer)}};
                for (const auto &r: rows) {
                    text += (text.empty() ? "" : "\n") + tr(r.first);
                    vals += (vals.empty() ? "" : "\n") + r.second;
                }
            }
            if (text != names->getText() || vals != values->getText()) {
                names->setText(text);
                values->setText(vals);
                redraw();
            }
        }

        ui::Label *heading;
        ui::Label *names;
        ui::Label *values;
        double lastUpdate = 0;
    };
}

namespace screens {
    Screen *makeDownloadDiagnostics(App &app) {
        return new DownloadDiagnosticsScreen(app);
    }

    Screen *makeSettings(App &app, SettingsPage page) {
        return new SettingsScreen(app, page);
    }

    Screen *makeAbout(App &app) {
        return new AboutScreen(app);
    }
}
