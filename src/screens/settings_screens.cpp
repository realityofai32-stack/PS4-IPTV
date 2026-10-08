// Settings (main page, Storage & downloads, Diagnostics) and About.
//
// Settings rows: caption on the left, the value in one right-aligned column - a choice (with dots showing
// its position among the options), a switch for On/Off settings, or a short info text for actions. The
// panel on the right explains the focused setting and its current value. Up/Down move, Left/Right or X
// change a value, X runs an action.
//
// Language changes apply at once: the screens are rebuilt (App::applyLanguage) and Settings opens again.

#include <algorithm>

#include "common.h"
#include "build_info.h"
#include "../downloads/download_model.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"

using namespace c2d;
using screens::SettingsPage;

namespace {

    const float LIST_X = theme::SAFE_X;
    const float LIST_Y = 200;
    const float LIST_W = 1100;
    const float LIST_H = 760;
    const float ROW_H = 88;
    const float ROW_GAP = 12;
    const float PANEL_X = LIST_X + LIST_W + 40;
    const float PANEL_W = theme::SCREEN_W - theme::SAFE_X - PANEL_X;
    const float VALUE_RIGHT = 32;      // right padding of the value column
    const float SWITCH_W = 76;
    const float SWITCH_H = 40;

    enum class Kind {
        Choice,
        Toggle,
        Action
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
        std::function<void()> run;                // Action
        std::function<std::string()> describe;    // text for the description panel
    };

    std::string sizeText(int64_t bytes) {
        if (bytes < 0) {
            return tr("common.unknown");
        }
        return bytes == 0 ? tr("settings.empty") : dl::formatBytes(bytes);
    }

    // one settings row (re-bound to different items while scrolling)
    class SettingRow : public RectangleShape {
    public:
        SettingRow(float w, float h) : RectangleShape(FloatRect(0, 0, w, h)) {
            setCornersRadius(theme::RADIUS_SMALL + 2);
            setCornerPointCount(8);
            caption = ui::label(this, "", theme::BODY, 32, ui::Label::centerOffset(theme::BODY, h));
            caption->setMaxWidth(w - 32 - 380);
            value = ui::label(this, "", theme::BODY, 0, ui::Label::centerOffset(theme::BODY, h), ui::Weight::SemiBold);
            value->setAlign(ui::Align::Right, w - VALUE_RIGHT);
            value->setMaxWidth(360);
            for (int i = 0; i < 6; i++) {
                auto *d = new CircleShape(4);
                d->setPointCount(12);
                d->setVisibility(Visibility::Hidden);
                add(d);
                dots.push_back(d);
            }
            float sx = w - VALUE_RIGHT - SWITCH_W;
            track = ui::box(this, FloatRect(sx, (h - SWITCH_H) / 2, SWITCH_W, SWITCH_H), theme::surfaceRaised(),
                            SWITCH_H / 2);
            knob = new CircleShape(SWITCH_H / 2 - 5);
            knob->setPointCount(24);
            track->add(knob);
            switchText = ui::label(this, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h),
                                   ui::Weight::Regular, theme::textDim());
            switchText->setAlign(ui::Align::Right, sx - 16);
        }

        void bind(const SettingItem &item, bool focused) {
            float w = getSize().x;
            float h = getSize().y;
            caption->setText(item.caption);
            caption->setColor(focused ? Color::White : theme::text());
            setFillColor(focused ? theme::rowFocus() : theme::surface());
            setOutlineColor(theme::accent());
            setOutlineThickness(focused ? 3 : 0);

            bool isToggle = item.kind == Kind::Toggle;
            track->setVisibility(isToggle ? Visibility::Visible : Visibility::Hidden);
            switchText->setVisibility(isToggle ? Visibility::Visible : Visibility::Hidden);
            value->setVisibility(isToggle ? Visibility::Hidden : Visibility::Visible);
            for (auto *d: dots) {
                d->setVisibility(Visibility::Hidden);
            }
            if (isToggle) {
                bool on = item.toggle();
                track->setFillColor(on ? theme::accent() : Color(74, 84, 102));
                knob->setFillColor(on ? Color::White : Color(196, 203, 214));
                float r = SWITCH_H / 2 - 5;
                knob->setPosition(on ? SWITCH_W - 5 - 2 * r : 5, 5);
                switchText->setText(tr(on ? "common.on" : "common.off"));
                switchText->setColor(focused ? theme::text() : theme::textDim());
                return;
            }
            if (item.kind == Kind::Choice) {
                int current = item.choice();
                // position dots only for short choices (language lists are long)
                int n = (int) item.options.size() <= (int) dots.size() ? (int) item.options.size() : 0;
                value->setText(item.options[(size_t) current]);
                value->setColor(focused ? Color::White : theme::text());
                value->setPosition(0, ui::Label::centerOffset(theme::BODY, h) - 7);
                // option position: small dots under the value, right-aligned with it
                for (int i = 0; i < n; i++) {
                    CircleShape *d = dots[(size_t) i];
                    d->setPosition(w - VALUE_RIGHT - (float) (n - i) * 16 + 8, h - 20);
                    d->setFillColor(i == current ? theme::accent() : theme::withAlpha(theme::textMuted(), 140));
                    d->setVisibility(Visibility::Visible);
                }
                return;
            }
            value->setText(item.info ? item.info() : "");
            value->setColor(focused ? theme::text() : theme::textMuted());
            value->setPosition(0, ui::Label::centerOffset(theme::BODY, h));
        }

    private:
        ui::Label *caption;
        ui::Label *value;
        std::vector<CircleShape *> dots;
        RectangleShape *track;
        CircleShape *knob;
        ui::Label *switchText;
    };

    class SettingsScreen : public Screen, public ui::ListView::Adapter {
    public:
        SettingsScreen(App &a, SettingsPage p) : Screen(a), page(p) {
            ui::background(this);
            screens::header(this, tr(page == SettingsPage::Storage ? "settings.storage_title"
                                     : page == SettingsPage::Diagnostics ? "settings.diagnostics_title"
                                                                         : "settings.title"));
            buildItems();
            list = new ui::ListView(FloatRect(LIST_X, LIST_Y, LIST_W, LIST_H), ROW_H, ROW_GAP, this);
            add(list);

            auto *panel = ui::box(this, FloatRect(PANEL_X, LIST_Y, PANEL_W, LIST_H), theme::surface(), theme::RADIUS);
            panelTitle = ui::label(panel, "", theme::HEADING, 36, 36, ui::Weight::SemiBold);
            panelTitle->setMaxWidth(PANEL_W - 72);
            panelTitle->setMaxLines(2);
            panelText = ui::label(panel, "", theme::BODY, 36, 0, ui::Weight::Regular, theme::textDim());
            panelText->setMaxWidth(PANEL_W - 72);
            panelText->setMaxLines(14);

            screens::hintBar(this, {{ui::Glyph::DPad, tr("settings.hint_change")}, {ui::Glyph::Cross, tr("common.select")},
                                    {ui::Glyph::Circle, tr("common.back")}});
            refreshStorage();
            refreshPanel();
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

        void tick(double now) override {
            // Storage page: sizes follow the downloads (at most once per second)
            if (page == SettingsPage::Storage && app.downloads().generation() != downloadsGen && now - lastStorage > 1) {
                lastStorage = now;
                refreshStorage();
                list->reload();
                refreshPanel();
                redraw();
            }
        }

        int count() override { return (int) items.size(); }

        C2DObject *createRow(float w, float h) override {
            return new SettingRow(w, h);
        }

        void bindRow(C2DObject *obj, int index, bool, bool focused) override {
            ((SettingRow *) obj)->bind(items[(size_t) index], focused);
        }

        void handleInput(const InputEvent &e) override {
            SettingItem &item = items[(size_t) list->selected()];
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
                    if (!e.repeat) {
                        change(item, e.button == PadButton::Right ? 1 : -1, false);
                    }
                    return;
                case PadButton::Cross:
                    if (!e.repeat) {
                        change(item, 1, true);
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

    private:
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
            } else if (activate && item.run) {
                item.run();
            } else {
                return;
            }
            list->reload();
            refreshPanel();
        }

        void refreshPanel() {
            const SettingItem &item = items[(size_t) list->selected()];
            panelTitle->setText(item.caption);
            panelText->setText(item.describe ? item.describe() : "");
            panelText->setPosition(36, 36 + panelTitle->height() + 20);
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

        void buildItems() {
            items.clear();
            switch (page) {
                case SettingsPage::Storage:
                    buildStorage();
                    break;
                case SettingsPage::Diagnostics:
                    buildDiagnostics();
                    break;
                default:
                    buildMain();
                    break;
            }
        }

        // ------------------------------------------------------------------ main page
        void buildMain() {
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

            SettingItem profiles;
            profiles.caption = tr("profiles.title");
            profiles.info = [] { return tr("settings.manage"); };
            profiles.run = [this] { app.push(screens::makeProfiles(app)); };
            profiles.describe = [] { return tr("settings.profiles.desc"); };
            items.push_back(profiles);

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

            // ---- movies / episodes: languages and subtitles
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

            // ---- video display (defaults; the Options panel changes only the current playback)
            SettingItem mode;
            mode.kind = Kind::Choice;
            mode.caption = tr("settings.display_mode");
            for (int i = 0; i < display::MODE_COUNT; i++) {
                mode.options.push_back(tr(display::modeNameKey((display::Mode) i)));
            }
            mode.choice = [&s] { return (int) s.displayMode; };
            mode.setChoice = [this, &s](int i) {
                s.displayMode = (display::Mode) i;
                save();
            };
            mode.describe = [&s] {
                return tr("settings.display_mode.desc", {tr(display::modeNameKey(s.displayMode)),
                                                         tr(display::modeDescKey(s.displayMode))});
            };
            items.push_back(mode);

            SettingItem zoom;
            zoom.kind = Kind::Choice;
            zoom.caption = tr("settings.zoom");
            for (int z: display::zoomSteps()) {
                zoom.options.push_back(std::to_string(z) + " %");
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
            };
            autoNext.describe = [] { return tr("settings.autoplay.desc"); };
            items.push_back(autoNext);

            // ---- downloads
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

            SettingItem storage;
            storage.caption = tr("settings.storage_title");
            storage.info = [this] { return sizeText(totals.completedBytes); };
            storage.run = [this] { app.push(screens::makeSettings(app, SettingsPage::Storage)); };
            storage.describe = [] { return tr("settings.storage.desc"); };
            items.push_back(storage);

            SettingItem diagnostics;
            diagnostics.caption = tr("settings.diagnostics_title");
            diagnostics.info = [] { return tr("settings.open"); };
            diagnostics.run = [this] { app.push(screens::makeSettings(app, SettingsPage::Diagnostics)); };
            diagnostics.describe = [] { return tr("settings.diagnostics.desc"); };
            items.push_back(diagnostics);

            SettingItem about;
            about.caption = tr("about.title");
            about.info = [] { return std::string("v") + APP_VERSION; };
            about.run = [this] { app.push(screens::makeAbout(app)); };
            about.describe = [] { return tr("settings.about.desc"); };
            items.push_back(about);
        }

        // ------------------------------------------------------------------ Storage & downloads
        void buildStorage() {
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

        // ------------------------------------------------------------------ Diagnostics
        void buildDiagnostics() {
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

        SettingsPage page;
        std::vector<SettingItem> items;
        ui::ListView *list = nullptr;
        ui::Label *panelTitle = nullptr;
        ui::Label *panelText = nullptr;
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
                               "Mozilla CA certificate bundle - MPL-2.0\n\n" + tr("about.notices");
            // i18n-exempt-end
            auto *body = ui::label(this, text, theme::LABEL, theme::SAFE_X, 210, ui::Weight::Regular, theme::textDim());
            body->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X);
            body->setMaxLines(24);
            screens::hintBar(this, {{ui::Glyph::Circle, tr("common.back")}});
        }

        const char *name() const override { return "about"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Circle || e.button == PadButton::Cross) {
                app.pop();
            }
        }
    };
}

namespace screens {
    Screen *makeSettings(App &app, SettingsPage page) {
        return new SettingsScreen(app, page);
    }

    Screen *makeAbout(App &app) {
        return new AboutScreen(app);
    }
}
