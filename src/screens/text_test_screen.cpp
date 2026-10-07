// Deterministic text rendering test (hardware checkpoint for the glyph upload fix, see ui/glyph_cache.h).
//
// Every line is fixed text in many sizes and both weights, inside the real widgets (buttons, form rows,
// truncated and wrapped panels). Two lines reveal one new character every 0.25 s at sizes no other text
// uses, so glyphs are added to the atlas after the first frame was drawn - exactly the case that lost
// letters on the PS4. A missing, partial or garbled letter anywhere means the renderer is still wrong.

#include <algorithm>
#include <cstdio>

#include "common.h"
#include "build_info.h"
#include "../core/utf8.h"
#include "../platform/log.h"

using namespace c2d;

namespace {

    const double REVEAL_STEP = 0.25;

    // one code point more every REVEAL_STEP seconds, then the full text stays
    class Reveal {
    public:
        Reveal(ui::Label *l, const std::string &text) : label(l), full(utf8::decode(text)) {}

        // true when the label changed
        bool tick(double elapsed) {
            size_t n = std::min(full.size(), (size_t) (elapsed / REVEAL_STEP) + 1);
            if (n == shown) {
                return false;
            }
            shown = n;
            std::string s;
            for (size_t i = 0; i < n; i++) {
                s += utf8::encode(full[i]);
            }
            label->setText(s);
            return true;
        }

        bool done() const { return shown == full.size(); }

    private:
        ui::Label *label;
        std::u32string full;
        size_t shown = 0;
    };

    class TextTestScreen : public Screen {
    public:
        TextTestScreen(App &a, bool atStartup) : Screen(a), startup(atStartup) {
            ui::background(this);
            screens::header(this, "Text rendering test",
                            std::string("Build ") + BUILD_GIT_HASH + "  \xE2\x80\xA2  " + BUILD_DATE
                            + "  \xE2\x80\xA2  PASS only if every letter on this screen is complete and sharp");

            // left column: fixed strings, sizes 18-64, both weights
            const float x = theme::SAFE_X;
            const char *upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            const char *lower = "abcdefghijklmnopqrstuvwxyz";
            const char *digits = "0123456789  :/._-@?=&%#+!";
            ui::label(this, "Add Xtream Profile", theme::DISPLAY, x, 165, ui::Weight::SemiBold);
            ui::label(this, "\xC3\x87\xC3\xA7 \xC4\x9E\xC4\x9F \xC4\xB0i I\xC4\xB1 \xC3\x96\xC3\xB6 "
                            "\xC5\x9E\xC5\x9F \xC3\x9C\xC3\xBC", theme::TITLE, x, 250, ui::Weight::SemiBold);
            ui::label(this, upper, theme::BODY, x, 310);
            ui::label(this, lower, theme::BODY, x, 345);
            ui::label(this, digits, theme::BODY, x, 380);
            ui::label(this, upper, theme::BODY, x, 420, ui::Weight::SemiBold);
            ui::label(this, lower, theme::BODY, x, 455, ui::Weight::SemiBold);
            ui::label(this, digits, theme::BODY, x, 490, ui::Weight::SemiBold);
            ui::label(this, "http://example.com:8080", theme::LABEL, x, 530);
            ui::label(this, "Username required", theme::LABEL, x, 560, ui::Weight::Regular, theme::danger());
            ui::label(this, "Password required", theme::LABEL, x, 590, ui::Weight::Regular, theme::danger());
            ui::label(this, "Fill in the fields, then use Test Connection.", theme::LABEL, x, 620,
                      ui::Weight::Regular, theme::textDim());
            ui::label(this, "Pijamal\xC4\xB1 hasta ya\xC4\x9F\xC4\xB1z \xC5\x9F" "of\xC3\xB6re \xC3\xA7" "abucak "
                            "g\xC3\xBCvendi.", theme::CAPTION, x, 655);
            ui::label(this, "P\xC4\xB0JAMALI HASTA YA\xC4\x9EIZ \xC5\x9EOF\xC3\x96RE \xC3\x87" "ABUCAK "
                            "G\xC3\x9CVEND\xC4\xB0.", theme::CAPTION, x, 683, ui::Weight::SemiBold);
            ui::label(this, std::string(upper) + " " + lower + " 0123456789", 20, x, 716);
            ui::label(this, std::string(upper) + " " + lower + " 0123456789", 18, x, 746, ui::Weight::SemiBold);
            ui::label(this, "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", theme::HEADING, x, 778);
            ui::label(this, "\xC3\x80\xC3\x81\xC3\x82\xC3\x84\xC3\x85\xC3\x89\xC3\x88\xC3\x8A\xC3\x8B\xC3\x8D"
                            "\xC3\x91\xC3\x93\xC3\x94\xC3\x98\xC3\x9A\xC3\x9F \xC3\xA0\xC3\xA1\xC3\xA2\xC3\xA4\xC3\xA5"
                            "\xC3\xA9\xC3\xA8\xC3\xAA\xC3\xAB\xC3\xAD\xC3\xB1\xC3\xB3\xC3\xB4\xC3\xB8\xC3\xBA\xC3\xBF "
                            "\xE2\x82\xAC\xC2\xA3\xC2\xA9\xC2\xB0\xC2\xB1\xC3\x97 \xE2\x80\xA6 \xE2\x80\x94",
                      26, x, 826);
            for (int i = 0; i < 3; i++) {  // the same string three times shares the cached glyphs
                ui::label(this, "Username required", theme::LABEL, x + (float) i * 324, 866, ui::Weight::SemiBold,
                          theme::textDim());
            }

            // right column: real widgets
            const float rx = 1140;
            const float rw = 684;
            ui::label(this, "Focused / unfocused / disabled", theme::CAPTION, rx, 165, ui::Weight::SemiBold,
                      theme::textMuted());
            addButton("Test Connection", FloatRect(rx, 200, 330, 80), true, true, true);
            addButton("Save", FloatRect(rx + 354, 200, 330, 80), true, false, true);
            addButton("Cancel", FloatRect(rx, 296, 330, 80), false, true, true);
            addButton("Delete", FloatRect(rx + 354, 296, 330, 80), false, false, false);
            auto *server = new screens::FieldRow(FloatRect(rx, 392, rw, 76), "Server");
            server->setValue("http://example.com:8080");
            server->setFocused(true);
            add(server);
            auto *user = new screens::FieldRow(FloatRect(rx, 480, rw, 76), "Username");
            user->setValue("required", true);
            add(user);

            ui::label(this, "Clipped panels (ellipsis, wrap)", theme::CAPTION, rx, 572, ui::Weight::SemiBold,
                      theme::textMuted());
            auto *p1 = ui::box(this, FloatRect(rx, 607, 330, 110), theme::surface(), theme::RADIUS_SMALL);
            auto *e1 = ui::label(p1, "Fill in the fields, then use Test Connection.", theme::LABEL, 20, 16);
            e1->setMaxWidth(290);
            auto *e2 = ui::label(p1, "http://example.com:8080/player_api.php", theme::CAPTION, 20, 60,
                                 ui::Weight::Regular, theme::textDim());
            e2->setMaxWidth(290);
            auto *p2 = ui::box(this, FloatRect(rx + 354, 607, 330, 110), theme::surface(), theme::RADIUS_SMALL);
            auto *w = ui::label(p2, "Fill in the fields, then use Test Connection.", theme::LABEL, 20, 16);
            w->setMaxWidth(290);
            w->setMaxLines(2);

            ui::label(this, "Characters added after the first frame", theme::CAPTION, rx, 732, ui::Weight::SemiBold,
                      theme::textMuted());
            auto *r1 = ui::label(this, "", 30, rx, 767, ui::Weight::SemiBold, theme::accent());
            r1->setMaxWidth(rw);
            reveals.emplace_back(r1, "\xC3\x87\xC4\x9E\xC4\xB0I\xC3\x96\xC5\x9E\xC3\x9C \xC3\xA7\xC4\x9Fi\xC4\xB1"
                                     "\xC3\xB6\xC5\x9F\xC3\xBC AQWXYZ aqwxyz 0123456789");
            auto *r2 = ui::label(this, "", 32, rx, 812);
            r2->setMaxWidth(rw);
            reveals.emplace_back(r2, "\xC5\x9E" "ehir Kanal\xC4\xB1 \xC4\xB0zle \xE2\x80\xA2 Ba\xC4\x9Flant\xC4\xB1 "
                                     "haz\xC4\xB1r");
            stats = ui::label(this, "", theme::CAPTION, rx, 868, ui::Weight::Regular, theme::textMuted());
            stats->setMaxWidth(rw);

            screens::hintBar(this, {{ui::Glyph::Cross, startup ? "Continue to the app" : "Close"},
                                    {ui::Glyph::Triangle, "Restart the live lines"}});
        }

        const char *name() const override { return "text-test"; }

        void onEnter() override {
            start = app.now();
        }

        void tick(double now) override {
            bool changed = false;
            for (auto &r: reveals) {
                changed |= r.tick(now - start);
            }
            ui::TextStats s = ui::textStats();
            char buf[160];
            snprintf(buf, sizeof(buf), "Atlas: %d pages  \xE2\x80\xA2  %d glyphs  \xE2\x80\xA2  %d uploads  \xE2\x80\xA2  "
                                       "%d grown  \xE2\x80\xA2  %d skipped", s.pages, s.glyphs, s.uploads, s.resizes,
                     s.unplaced);
            if (stats->getText() != buf) {
                stats->setText(buf);
                changed = true;
            }
            if (changed) {
                redraw();
            }
        }

        bool animating() const override {
            for (const auto &r: reveals) {
                if (!r.done()) {
                    return true;
                }
            }
            return false;
        }

        void handleInput(const InputEvent &e) override {
            if (e.repeat) {
                return;
            }
            if (e.button == PadButton::Triangle) {
                start = app.now();
            } else if (e.button == PadButton::Cross || (!startup && e.button == PadButton::Circle)) {
                ui::TextStats s = ui::textStats();
                LOG_I("ui", "text test closed: %d pages, %d glyphs, %d uploads, %d grown, %d skipped", s.pages,
                      s.glyphs, s.uploads, s.resizes, s.unplaced);
                if (startup) {
                    app.replaceAll(app.firstScreen());
                } else {
                    app.pop();
                }
            }
        }

    private:
        void addButton(const std::string &text, const FloatRect &rect, bool primary, bool focused, bool enabled) {
            auto *b = new ui::Button(text, rect, primary);
            b->setFocused(focused);
            b->setEnabled(enabled);
            add(b);
        }

        bool startup;
        double start = 0;
        std::vector<Reveal> reveals;
        ui::Label *stats;
    };
}

namespace screens {
    Screen *makeTextTest(App &app, bool atStartup) {
        return new TextTestScreen(app, atStartup);
    }
}
