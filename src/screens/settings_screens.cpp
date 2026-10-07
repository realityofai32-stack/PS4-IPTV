// Settings and About.
//
// Settings rows: caption on the left, the value in one right-aligned column - a choice (with dots showing
// its position among the options), a switch for On/Off settings, or a short info text for actions. The
// panel on the right explains the focused setting and its current value. Up/Down move, Left/Right or X
// change a value, X runs an action.

#include "common.h"
#include "build_info.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"

using namespace c2d;

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

    std::string megabytes(int64_t bytes) {
        if (bytes < 0) {
            return "\xE2\x80\xA6";
        }
        if (bytes < 1024 * 1024) {
            return bytes == 0 ? "Empty" : std::to_string((bytes + 1023) / 1024) + " KB";
        }
        return diag::format("%.1f MB", (double) bytes / (1024.0 * 1024.0));
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
            for (int i = 0; i < 4; i++) {
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
                switchText->setText(on ? "On" : "Off");
                switchText->setColor(focused ? theme::text() : theme::textDim());
                return;
            }
            if (item.kind == Kind::Choice) {
                int current = item.choice();
                int n = std::min((int) item.options.size(), (int) dots.size());
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
        explicit SettingsScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, "Settings");
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

            screens::hintBar(this, {{ui::Glyph::DPad, "Change"}, {ui::Glyph::Cross, "Select"},
                                    {ui::Glyph::Circle, "Back"}});
            refreshPanel();
        }

        const char *name() const override { return "settings"; }

        void onEnter() override {
            refreshImageCacheSize();
        }

        void onResume() override {
            list->reload();
            refreshPanel();
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
                app.toast("Could not save settings", ToastKind::Error);
            }
        }

        void refreshImageCacheSize() {
            app.images().refreshDiskUsage(guarded([this] {
                list->reload();
                redraw();
            }));
        }

        void confirm(const std::string &title, const std::string &message, std::function<void()> action) {
            app.push(screens::makeDialog(app, title, message, {"Cancel", "Clear"}, [action](int c) {
                if (c == 1) {
                    action();
                }
            }, true));
        }

        void buildItems() {
            Settings &s = app.settings().get();
            items.clear();

            SettingItem profiles;
            profiles.caption = "Profiles";
            profiles.info = [] { return std::string("Manage"); };
            profiles.run = [this] { app.push(screens::makeProfiles(app)); };
            profiles.describe = [] {
                return std::string("Add, edit or switch between your IPTV provider accounts.");
            };
            items.push_back(profiles);

            SettingItem format;
            format.kind = Kind::Choice;
            format.caption = "Live stream format";
            format.options = {"Auto", "Prefer TS", "Prefer HLS"};
            format.choice = [&s] { return s.streamFormat == StreamFormat::Ts ? 1 : s.streamFormat == StreamFormat::Hls ? 2 : 0; };
            format.setChoice = [this, &s](int i) {
                s.streamFormat = i == 1 ? StreamFormat::Ts : i == 2 ? StreamFormat::Hls : StreamFormat::Auto;
                save();
            };
            format.describe = [&s] {
                switch (s.streamFormat) {
                    case StreamFormat::Ts:
                        return std::string("Prefer TS: channels open as MPEG-TS first. If a channel cannot be "
                                           "opened as TS, HLS is tried once.");
                    case StreamFormat::Hls:
                        return std::string("Prefer HLS: channels open as HLS first. HLS loads the stream in "
                                           "segments, which can ride out short Wi-Fi drops better on some providers; "
                                           "it usually starts a little later. If HLS cannot be opened, TS is tried "
                                           "once.");
                    default:
                        return std::string("Auto: MPEG-TS first, HLS once if TS cannot be opened. When only the "
                                           "fallback works, the app uses that format first for the rest of the "
                                           "session.");
                }
            };
            items.push_back(format);

            SettingItem stab;
            stab.kind = Kind::Choice;
            stab.caption = "Playback stability";
            stab.options = {"Fast", "Balanced", "Maximum stability"};
            stab.choice = [&s] { return s.stability == StabilityPreset::Fast ? 0 : s.stability == StabilityPreset::MaxStability ? 2 : 1; };
            stab.setChoice = [this, &s](int i) {
                s.stability = i == 0 ? StabilityPreset::Fast : i == 2 ? StabilityPreset::MaxStability
                                                                      : StabilityPreset::Balanced;
                save();
            };
            stab.describe = [&s] {
                switch (s.stability) {
                    case StabilityPreset::Fast:
                        return std::string("Fast: channels start as soon as data arrives, with a small buffer. "
                                           "Best on a fast, steady connection; short network drops may freeze the "
                                           "picture sooner.");
                    case StabilityPreset::MaxStability:
                        return std::string("Maximum stability: about 8 seconds are buffered before a channel "
                                           "starts and after every interruption, and the player waits longer "
                                           "before reconnecting. Slower to start and further behind live, but "
                                           "the most tolerant of weak Wi-Fi.");
                    default:
                        return std::string("Balanced: about 3 seconds are buffered before a channel starts and "
                                           "after an interruption, enough for normal Wi-Fi jitter. Stalls are "
                                           "detected and the stream reconnects automatically.");
                }
            };
            items.push_back(stab);

            SettingItem retry;
            retry.kind = Kind::Toggle;
            retry.caption = "Retry on stall";
            retry.toggle = [&s] { return s.retryOnStall; };
            retry.setToggle = [this, &s](bool on) {
                s.retryOnStall = on;
                save();
            };
            retry.describe = [&s] {
                return std::string(s.retryOnStall
                                   ? "On: when a stream stops progressing or the connection drops, the player "
                                     "reconnects by itself a limited number of times, waiting longer after each try. "
                                     "If the provider refuses the stream (HTTP 403), it counts down and retries."
                                   : "Off: the player keeps buffering after a stall and never reconnects by "
                                     "itself. Press X during playback to reconnect.");
            };
            items.push_back(retry);

            SettingItem tech;
            tech.kind = Kind::Toggle;
            tech.caption = "Show technical playback info";
            tech.toggle = [&s] { return s.showTechnicalInfo; };
            tech.setToggle = [this, &s](bool on) {
                s.showTechnicalInfo = on;
                save();
            };
            tech.describe = [] {
                return std::string("Shows the stream information panel (resolution, codecs, buffer, network speed, "
                                   "reconnect attempts) when playback starts. Triangle toggles it at any time.");
            };
            items.push_back(tech);

            SettingItem images;
            images.kind = Kind::Toggle;
            images.caption = "Load logos and posters";
            images.toggle = [&s] { return s.loadImages; };
            images.setToggle = [this, &s](bool on) {
                s.loadImages = on;
                app.images().setEnabled(on);
                save();
            };
            images.describe = [] {
                return std::string("Downloads channel logos from your provider and keeps them on the console. "
                                   "Off: channels show their initials and no images are downloaded.");
            };
            items.push_back(images);

            SettingItem resume;
            resume.kind = Kind::Toggle;
            resume.caption = "Resume movies and episodes";
            resume.toggle = [&s] { return s.resumeVod; };
            resume.setToggle = [this, &s](bool on) {
                s.resumeVod = on;
                save();
            };
            resume.describe = [] {
                return std::string("Continue movies and episodes where you stopped. Takes effect when Movies and "
                                   "Series arrive in a later version.");
            };
            items.push_back(resume);

            SettingItem clearImages;
            clearImages.caption = "Clear image cache";
            clearImages.info = [this] { return megabytes(app.images().diskBytes()); };
            clearImages.run = [this] {
                confirm("Clear image cache?", "Channel logos are downloaded again when they are needed.", [this] {
                    std::function<void()> done = guarded([this] {
                        app.toast("Image cache cleared", ToastKind::Success);
                        list->reload();
                    });
                    app.images().clearCache([done](int64_t) { done(); });
                });
            };
            clearImages.describe = [] {
                return std::string("Deletes the channel logos stored on the console (at most 48 MB; the oldest are "
                                   "removed automatically when it is full).");
            };
            items.push_back(clearImages);

            SettingItem clearMeta;
            clearMeta.caption = "Clear metadata cache";
            clearMeta.run = [this] {
                confirm("Clear metadata cache?", "The saved channel lists are deleted. The list loaded now stays "
                                                 "until you reconnect.", [this] {
                    app.jobs().submit(JobPriority::High, "clear-metadata", [](const CancelToken &) {
                        CatalogCache(APP_DATA_DIR).clearAll();
                    }, guarded([this] {
                        LOG_I("settings", "metadata cache cleared");
                        app.toast("Metadata cache cleared", ToastKind::Success);
                    }));
                });
            };
            clearMeta.describe = [] {
                return std::string("Deletes the saved copy of your channel lists, which is used when the provider "
                                   "cannot be reached at startup.");
            };
            items.push_back(clearMeta);

            SettingItem clearHistory;
            clearHistory.caption = "Clear watch history";
            clearHistory.info = [this] {
                size_t n = app.library().history().size();
                return n == 0 ? std::string("Empty") : std::to_string(n) + (n == 1 ? " entry" : " entries");
            };
            clearHistory.run = [this] {
                if (app.library().history().empty()) {
                    app.toast("Watch history is empty");
                    return;
                }
                confirm("Clear watch history?", "Your recently watched channels are removed for this profile.",
                        [this] {
                            app.library().clearHistory();
                            app.saveLibrary();
                            app.toast("Watch history cleared", ToastKind::Success);
                            list->reload();
                        });
            };
            clearHistory.describe = [] {
                return std::string("Removes the list of recently watched channels of the active profile. Favorites "
                                   "are kept.");
            };
            items.push_back(clearHistory);

            SettingItem textTest;
            textTest.caption = "Text rendering test";
            textTest.run = [this] { app.push(screens::makeTextTest(app, false)); };
            textTest.describe = [] {
                return std::string("A diagnostic page with sample text in several languages and sizes, to check "
                                   "the on-screen font rendering.");
            };
            items.push_back(textTest);

            SettingItem about;
            about.caption = "About PS4 IPTV";
            about.info = [] { return std::string("v") + APP_VERSION; };
            about.run = [this] { app.push(screens::makeAbout(app)); };
            about.describe = [] {
                return std::string("Version, build and the open-source projects PS4 IPTV is built on.");
            };
            items.push_back(about);
        }

        std::vector<SettingItem> items;
        ui::ListView *list = nullptr;
        ui::Label *panelTitle = nullptr;
        ui::Label *panelText = nullptr;
    };

    class AboutScreen : public Screen {
    public:
        explicit AboutScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, "About PS4 IPTV", std::string("Version ") + APP_VERSION + "  \xE2\x80\xA2  build "
                                                    + BUILD_DATE + "  \xE2\x80\xA2  " + BUILD_GIT_HASH);
            const char *text =
                    "PS4 IPTV is free software: you can redistribute it and/or modify it under the terms of the GNU "
                    "General Public License as published by the Free Software Foundation, version 3 or later. It is "
                    "distributed WITHOUT ANY WARRANTY. See LICENSE (GPL-3.0).\n"
                    "PS4 IPTV does not provide any content. Use it only with services you are entitled to access.\n"
                    "\n"
                    "Built on these projects - thank you:\n"
                    "pPlay (Cpasjuste): PS4 playback integration - GPL-3.0\n"
                    "libcross2d (Cpasjuste): rendering, input and platform layer - GPL-3.0\n"
                    "mpv 0.34.1 with PS4 patches (PacBrew): media player library - GPL-2.0-or-later\n"
                    "FFmpeg 5.0: decoding, demuxing, network protocols - LGPL-2.1-or-later / GPL\n"
                    "SDL 2.0.18 PS4 port (PacBrew): video, audio, controller - zlib\n"
                    "libass, FreeType, FriBidi, libpng, zlib, bzip2, Opus - ISC / FTL / LGPL / BSD / zlib\n"
                    "stb_image (Sean Barrett, via libcross2d): channel logo decoding - public domain / MIT\n"
                    "libcurl 7.80 (curl license) and Mbed TLS 2.16 (Apache-2.0): HTTP(S) for the Xtream API and logos\n"
                    "OpenOrbis PS4 Toolchain (GPL-3.0), PacBrew musl (MIT), libc++ (Apache-2.0 with LLVM exception)\n"
                    "Inter typeface by Rasmus Andersson - SIL Open Font License 1.1\n"
                    "Mozilla CA certificate bundle - MPL-2.0\n"
                    "\n"
                    "Licenses and notices: LICENSE and NOTICE in the project source.";
            auto *body = ui::label(this, text, theme::LABEL, theme::SAFE_X, 210, ui::Weight::Regular, theme::textDim());
            body->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X);
            body->setMaxLines(24);
            screens::hintBar(this, {{ui::Glyph::Circle, "Back"}});
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
    Screen *makeSettings(App &app) {
        return new SettingsScreen(app);
    }

    Screen *makeAbout(App &app) {
        return new AboutScreen(app);
    }
}
