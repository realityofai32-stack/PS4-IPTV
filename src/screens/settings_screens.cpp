// Settings and About.

#include "common.h"
#include "build_info.h"
#include "../platform/log.h"

using namespace c2d;

namespace {

    // simple scrolling text/option list rows
    struct SettingRow {
        std::string caption;
        std::function<std::string()> value;   // empty function: action row
        std::function<void()> activate;
    };

    class SettingsScreen : public Screen, public ui::ListView::Adapter {
    public:
        explicit SettingsScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, "Settings");
            Settings &s = app.settings().get();
            items = {
                    {"Profiles", [] { return std::string("Manage"); }, [this] {
                        app.push(screens::makeProfiles(app));
                    }},
                    {"Live stream format", [&s] {
                        return std::string(s.streamFormat == StreamFormat::Ts ? "MPEG-TS"
                                           : s.streamFormat == StreamFormat::Hls ? "HLS" : "Auto (TS, then HLS)");
                    }, [this, &s] {
                        s.streamFormat = s.streamFormat == StreamFormat::Auto ? StreamFormat::Ts
                                         : s.streamFormat == StreamFormat::Ts ? StreamFormat::Hls : StreamFormat::Auto;
                        save();
                    }},
                    {"Resume movies and episodes", [&s] { return std::string(s.resumeVod ? "On" : "Off"); },
                     [this, &s] {
                         s.resumeVod = !s.resumeVod;
                         save();
                     }},
                    {"Show technical playback info", [&s] { return std::string(s.showTechnicalInfo ? "On" : "Off"); },
                     [this, &s] {
                         s.showTechnicalInfo = !s.showTechnicalInfo;
                         save();
                     }},
                    {"Load logos and posters", [&s] { return std::string(s.loadImages ? "On" : "Off"); },
                     [this, &s] {
                         s.loadImages = !s.loadImages;
                         save();
                     }},
                    {"Clear image cache", nullptr, [this] { app.toast("Image cache is empty"); }},
                    {"Clear metadata cache", nullptr, [this] { app.toast("Metadata cache is empty"); }},
                    {"Clear watch history", nullptr, [this] { app.toast("Watch history is empty"); }},
                    {"Text rendering test", nullptr, [this] { app.push(screens::makeTextTest(app, false)); }},
                    {"About PS4 IPTV", nullptr, [this] { app.push(screens::makeAbout(app)); }},
            };
            list = new ui::ListView(FloatRect(theme::SAFE_X, 200, 1300, 780), 84, 12, this);
            add(list);
            screens::hintBar(this, {{ui::Glyph::Cross, "Change / open"}, {ui::Glyph::Circle, "Back"}});
        }

        const char *name() const override { return "settings"; }

        int count() override { return (int) items.size(); }

        C2DObject *createRow(float w, float h) override {
            auto *r = new screens::FieldRow(FloatRect(0, 0, w, h), "");
            rows.push_back(r);
            return r;
        }

        void bindRow(C2DObject *obj, int index, bool, bool focused) override {
            auto *r = (screens::FieldRow *) obj;
            const SettingRow &item = items[(size_t) index];
            r->setCaption(item.caption);
            r->setValue(item.value ? item.value() : "\xE2\x80\xBA", false);  // action rows show a chevron
            r->setFocused(focused);
        }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Up) {
                list->moveSelection(-1);
            } else if (e.button == PadButton::Down) {
                list->moveSelection(1);
            } else if (e.button == PadButton::Cross && !e.repeat) {
                items[(size_t) list->selected()].activate();
                list->reload();
            } else if (e.button == PadButton::Circle) {
                app.pop();
            }
        }

    private:
        void save() {
            std::string err;
            if (!app.settings().save(&err)) {
                LOG_E("settings", "save failed: %s", err.c_str());
                app.toast("Could not save settings", ToastKind::Error);
            }
        }

        std::vector<SettingRow> items;
        std::vector<screens::FieldRow *> rows;
        ui::ListView *list;
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
                    "libcurl 7.80 (curl license) and Mbed TLS 2.16 (Apache-2.0): HTTP(S) for the Xtream API\n"
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
