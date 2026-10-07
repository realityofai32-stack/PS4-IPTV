// Home dashboard and section placeholders.

#include "common.h"
#include "../platform/clock.h"

using namespace c2d;
using namespace iptv;

namespace {

    class HomeScreen : public Screen {
    public:
        explicit HomeScreen(App &a) : Screen(a) {
            ui::background(this);
            const Session &s = app.session();

            // top bar
            ui::label(this, "PS4 IPTV", theme::HEADING, theme::SAFE_X, theme::SAFE_Y, ui::Weight::SemiBold,
                      theme::accent());
            clock = ui::label(this, clockx::localTime(), theme::HEADING, 0, theme::SAFE_Y, ui::Weight::SemiBold);
            clock->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);
            const float whoRight = theme::SCREEN_W - theme::SAFE_X - 180;
            auto *who = ui::label(this, s.profile.name, theme::BODY, 0, theme::SAFE_Y + 4, ui::Weight::SemiBold,
                                  theme::textDim());
            who->setMaxWidth(480);
            who->setAlign(ui::Align::Right, whoRight);
            auto *dot = new CircleShape(8);
            dot->setPointCount(16);
            dot->setFillColor(s.connected ? theme::success() : theme::danger());
            dot->setPosition(whoRight - who->width() - 28, theme::SAFE_Y + 15);
            add(dot);

            // account notice
            std::string notice;
            Color noticeColor = theme::warning();
            int64_t now = clockx::unixNow();
            if (s.httpsWarning) {
                notice = "This server uses HTTPS. This build currently supports HTTP streams only.";
            } else if (s.account.expiresAt > 0 && s.account.expiresAt - now < 7 * 86400) {
                notice = "Your subscription expires on " + clockx::localDate(s.account.expiresAt) + ".";
            }
            if (!notice.empty()) {
                auto *n = ui::label(this, notice, theme::LABEL, theme::SAFE_X, 120, ui::Weight::Regular, noticeColor);
                n->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X);
            }

            // section tiles
            struct TileDef {
                const char *title;
                std::string subtitle;
            };
            auto count = [&s](int i) {
                return s.categoriesLoaded[i] ? std::to_string(s.categories[i].size()) + " categories"
                                             : std::string("Unavailable");
            };
            TileDef defs[] = {{"Live TV", count(0)},
                              {"Movies", count(1)},
                              {"Series", count(2)},
                              {"Favorites", "Your saved picks"},
                              {"Search", "Channels, movies, series"},
                              {"Settings", "Profiles and playback"}};
            const float gap = 24;
            const float tileW = (theme::SCREEN_W - 2 * theme::SAFE_X - 5 * gap) / 6;
            for (int i = 0; i < 6; i++) {
                Tile &t = tiles[i];
                t.bg = ui::box(this, FloatRect(theme::SAFE_X + (float) i * (tileW + gap), 180, tileW, 230),
                               theme::surface(), 20);
                t.accentBar = ui::box(t.bg, FloatRect(28, 32, 44, 6), theme::accent(), 3);
                t.title = ui::label(t.bg, defs[i].title, theme::HEADING, 28, 120, ui::Weight::SemiBold);
                t.title->setMaxWidth(tileW - 56);
                t.subtitle = ui::label(t.bg, defs[i].subtitle, theme::LABEL, 28, 172, ui::Weight::Regular,
                                       theme::textDim());
                t.subtitle->setMaxWidth(tileW - 56);
            }

            // rows (filled by later milestones: continue watching / history)
            row(470, "Continue Watching", "Movies and episodes you start will appear here, ready to resume.");
            row(730, "Recently Watched", "Channels, movies and episodes you watch will appear here.");

            hints = screens::hintBar(this, {{ui::Glyph::Cross, "Open"}, {ui::Glyph::Triangle, "Search"},
                                            {ui::Glyph::Options, "Settings"}, {ui::Glyph::Circle, "Exit"}});
            refresh();
        }

        const char *name() const override { return "home"; }

        void tick(double) override {
            std::string t = clockx::localTime();
            if (t != clock->getText()) {
                clock->setText(t);
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Left:
                case PadButton::L1:
                    if (focus > 0) {
                        focus--;
                    }
                    break;
                case PadButton::Right:
                case PadButton::R1:
                    if (focus < 5) {
                        focus++;
                    }
                    break;
                case PadButton::Cross:
                    if (!e.repeat) {
                        open(focus);
                    }
                    return;
                case PadButton::Triangle:
                    open(4);
                    return;
                case PadButton::Options:
                    open(5);
                    return;
                case PadButton::Circle:
                    app.push(screens::makeDialog(app, "Exit PS4 IPTV?", "", {"Cancel", "Exit"}, [this](int c) {
                        if (c == 1) {
                            app.quit();
                        }
                    }));
                    return;
                default:
                    return;
            }
            refresh();
        }

    private:
        struct Tile {
            RectangleShape *bg;
            RectangleShape *accentBar;
            ui::Label *title;
            ui::Label *subtitle;
        };

        void row(float y, const char *title, const char *empty) {
            ui::label(this, title, theme::HEADING, theme::SAFE_X, y, ui::Weight::SemiBold);
            auto *card = ui::box(this, FloatRect(theme::SAFE_X, y + 62, theme::SCREEN_W - 2 * theme::SAFE_X, 150),
                                 theme::withAlpha(theme::surface(), 160), theme::RADIUS);
            auto *l = ui::label(card, empty, theme::BODY, 40, ui::Label::centerOffset(theme::BODY, 150),
                                ui::Weight::Regular, theme::textMuted());
            l->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X - 80);
        }

        void open(int index) {
            switch (index) {
                case 0:
                    app.push(screens::makeSection(app, "Live TV", "Live TV channels arrive in the next milestone of "
                                                                  "this build."));
                    break;
                case 1:
                    app.push(screens::makeSection(app, "Movies", "Movies arrive in a later milestone."));
                    break;
                case 2:
                    app.push(screens::makeSection(app, "Series", "Series arrive in a later milestone."));
                    break;
                case 3:
                    app.push(screens::makeSection(app, "Favorites", "Favorites arrive with Live TV."));
                    break;
                case 4:
                    app.push(screens::makeSection(app, "Search", "Search arrives with Live TV."));
                    break;
                default:
                    app.push(screens::makeSettings(app));
                    break;
            }
        }

        void refresh() {
            for (int i = 0; i < 6; i++) {
                bool f = i == focus;
                Tile &t = tiles[i];
                t.bg->setFillColor(f ? theme::accentDark() : theme::surface());
                t.bg->setOutlineColor(theme::withAlpha(Color::White, 220));
                t.bg->setOutlineThickness(f ? theme::FOCUS_BORDER : 0);
                t.accentBar->setFillColor(f ? Color::White : theme::accent());
                t.subtitle->setColor(f ? theme::text() : theme::textDim());
            }
        }

        Tile tiles[6];
        ui::Label *clock;
        ui::HintBar *hints;
        int focus = 0;
    };

    class SectionScreen : public Screen {
    public:
        SectionScreen(App &a, const std::string &title, const std::string &message) : Screen(a) {
            ui::background(this);
            screens::header(this, title);
            auto *m = ui::label(this, message, theme::BODY, 0, 480, ui::Weight::Regular, theme::textDim());
            m->setAlign(ui::Align::Center, theme::SCREEN_W);
            m->setMaxWidth(1200);
            m->setMaxLines(3);
            screens::hintBar(this, {{ui::Glyph::Circle, "Back"}});
        }

        const char *name() const override { return "section"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Circle || e.button == PadButton::Cross) {
                app.pop();
            }
        }
    };
}

namespace screens {
    Screen *makeHome(App &app) {
        return new HomeScreen(app);
    }

    Screen *makeSection(App &app, const std::string &title, const std::string &message) {
        return new SectionScreen(app, title, message);
    }
}
