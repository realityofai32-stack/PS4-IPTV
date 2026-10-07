// Sign-in and initial loading: account, then Live / Movies / Series categories.

#include "common.h"
#include "../iptv/xtream.h"
#include "../platform/clock.h"
#include "../platform/log.h"

using namespace c2d;
using namespace iptv;

namespace {

    class ConnectScreen : public Screen {
    public:
        ConnectScreen(App &a, const Profile &p) : Screen(a), profile(p) {
            ui::background(this);
            auto *t = ui::label(this, "Connecting to " + p.name, theme::TITLE, 0, 230, ui::Weight::SemiBold);
            t->setAlign(ui::Align::Center, theme::SCREEN_W);
            t->setMaxWidth(1400);
            auto *h = ui::label(this, screens::hostOf(p.server), theme::BODY, 0, 300, ui::Weight::Regular,
                                theme::textDim());
            h->setAlign(ui::Align::Center, theme::SCREEN_W);
            const char *names[] = {"Account", "Live TV", "Movies", "Series"};
            float x = (theme::SCREEN_W - 760) / 2;
            for (int i = 0; i < 4; i++) {
                Step &s = steps[i];
                float y = 400 + (float) i * 92;
                s.bg = ui::box(this, FloatRect(x, y, 760, 76), theme::surface(), theme::RADIUS_SMALL);
                s.spinner = new ui::Spinner(14);
                s.spinner->setPosition(30, 31);
                s.bg->add(s.spinner);
                s.mark = ui::label(s.bg, "", theme::HEADING, 30, ui::Label::centerOffset(theme::HEADING, 76),
                                   ui::Weight::SemiBold);
                ui::label(s.bg, names[i], theme::BODY, 110, ui::Label::centerOffset(theme::BODY, 76),
                          ui::Weight::SemiBold);
                s.detail = ui::label(s.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, 76),
                                     ui::Weight::Regular, theme::textDim());
                s.detail->setAlign(ui::Align::Right, 730);
                s.detail->setMaxWidth(470);
                setStep(i, State::Pending, "");
            }
            message = ui::label(this, "", theme::BODY, 0, 790, ui::Weight::Regular, theme::danger());
            message->setAlign(ui::Align::Center, theme::SCREEN_W);
            message->setMaxWidth(1300);
            message->setMaxLines(3);
            const char *labels[] = {"Retry", "Edit profile", "Profiles"};
            float bx = (theme::SCREEN_W - (3 * 300 + 2 * 24)) / 2;
            for (int i = 0; i < 3; i++) {
                buttons[i] = new ui::Button(labels[i], FloatRect(bx + (float) i * 324, 900, 300, 80), i == 0);
                buttons[i]->setVisibility(Visibility::Hidden);
                add(buttons[i]);
            }
            hints = screens::hintBar(this, {{ui::Glyph::Circle, "Cancel"}});
        }

        ~ConnectScreen() override {
            token.cancel();
        }

        const char *name() const override { return "connect"; }

        void onEnter() override {
            start();
        }

        void tick(double now) override {
            bool changed = false;
            for (auto &s: steps) {
                if (s.state == State::Running) {
                    changed |= s.spinner->tick(now);
                }
            }
            if (changed) {
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            if (failed) {
                if (e.button == PadButton::Left && focus > 0) {
                    focus--;
                } else if (e.button == PadButton::Right && focus < 2) {
                    focus++;
                } else if (e.button == PadButton::Cross && !e.repeat) {
                    if (focus == 0) {
                        start();
                    } else if (focus == 1) {
                        app.replaceAll(screens::makeProfiles(app));
                        app.push(screens::makeProfileEdit(app, profile));
                    } else {
                        app.replaceAll(screens::makeProfiles(app));
                    }
                    return;
                } else if (e.button == PadButton::Circle) {
                    app.replaceAll(screens::makeProfiles(app));
                    return;
                }
                for (int i = 0; i < 3; i++) {
                    buttons[i]->setFocused(i == focus);
                }
            } else if (e.button == PadButton::Circle) {
                token.cancel();
                LOG_I("connect", "canceled by the user");
                app.replaceAll(screens::makeProfiles(app));
            }
        }

    private:
        enum class State {
            Pending,
            Running,
            Done,
            Failed
        };

        struct Step {
            RectangleShape *bg;
            ui::Spinner *spinner;
            ui::Label *mark;
            ui::Label *detail;
            State state = State::Pending;
        };

        void setStep(int i, State state, const std::string &detail) {
            Step &s = steps[i];
            s.state = state;
            s.spinner->setVisibility(state == State::Running ? Visibility::Visible : Visibility::Hidden);
            s.mark->setText(state == State::Done ? "\xE2\x9C\x93" : state == State::Failed ? "!" : "");
            s.mark->setColor(state == State::Done ? theme::success() : theme::danger());
            s.detail->setText(detail);
            s.detail->setColor(state == State::Failed ? theme::danger() : theme::textDim());
            s.bg->setFillColor(state == State::Running ? theme::surfaceRaised() : theme::surface());
            redraw();
        }

        void start() {
            failed = false;
            message->setText("");
            for (auto *b: buttons) {
                b->setVisibility(Visibility::Hidden);
            }
            hints->setHints({{ui::Glyph::Circle, "Cancel"}});
            for (int i = 0; i < 4; i++) {
                setStep(i, State::Pending, "");
            }
            app.session() = Session();
            setStep(0, State::Running, "Signing in" "\xE2\x80\xA6");
            // the token is canceled in the destructor, so the callback never runs after this screen is gone
            token = app.xtream().authenticate(profile, [this](const XtreamService::AuthOutcome &o) {
                onAuth(o);
            });
        }

        void onAuth(const XtreamService::AuthOutcome &o) {
            int64_t now = clockx::unixNow();
            app.profiles().setLastStatus(profile.id, o.message, now);
            app.profiles().save();
            if (o.result.status != AuthStatus::Ok) {
                setStep(0, State::Failed, o.message);
                std::string msg = o.message;
                if (o.result.status == AuthStatus::Expired && o.result.account.expiresAt > 0) {
                    msg += " (expired on " + clockx::localDate(o.result.account.expiresAt) + ")";
                } else if (o.result.status == AuthStatus::Network) {
                    msg += ". Check the server address and the PS4 network connection.";
                }
                fail(msg);
                return;
            }
            Session &s = app.session();
            s.profile = profile;
            s.account = o.result.account;
            s.httpsWarning = o.httpsWarning;
            setStep(0, State::Done, o.result.account.expiresAt > 0
                                    ? "Active until " + clockx::localDate(o.result.account.expiresAt) : "Active");
            loadCategories(0);
        }

        void loadCategories(int index) {
            if (index >= 3) {
                Session &s = app.session();
                s.connected = true;
                LOG_I("connect", "ready: live %d, movie %d, series %d categories", (int) s.categories[0].size(),
                      (int) s.categories[1].size(), (int) s.categories[2].size());
                app.replaceAll(screens::makeHome(app));
                return;
            }
            setStep(index + 1, State::Running, "Loading" "\xE2\x80\xA6");
            token = app.xtream().loadCategories(profile, (ContentType) index,
                                                [this, index](const XtreamService::CategoriesOutcome &o) {
                Session &s = app.session();
                if (o.ok) {
                    s.categories[index] = o.categories;
                    s.categoriesLoaded[index] = true;
                    setStep(index + 1, State::Done, std::to_string(o.categories.size()) + " categories");
                } else {
                    // not fatal: the section shows the error and can retry later
                    setStep(index + 1, State::Failed, o.message);
                }
                loadCategories(index + 1);
            });
        }

        void fail(const std::string &msg) {
            failed = true;
            message->setText(msg);
            focus = 0;
            for (int i = 0; i < 3; i++) {
                buttons[i]->setVisibility(Visibility::Visible);
                buttons[i]->setFocused(i == focus);
            }
            hints->setHints({{ui::Glyph::Cross, "Select"}, {ui::Glyph::Circle, "Profiles"}});
        }

        Profile profile;
        Step steps[4];
        ui::Label *message;
        ui::Button *buttons[3];
        ui::HintBar *hints;
        CancelToken token;
        bool failed = false;
        int focus = 0;
    };
}

namespace screens {
    Screen *makeConnect(App &app, const Profile &profile) {
        return new ConnectScreen(app, profile);
    }
}
