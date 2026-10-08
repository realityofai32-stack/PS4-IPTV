// Sign-in and initial loading: account, then Live categories + streams. Movies and Series are loaded lazily when
// their section opens.
//
// When the provider cannot be reached (no network, server down, expired account...), "Continue offline" opens
// Home with the saved channel / movie / series lists and the Downloads section: completed downloads play
// without any provider request. It is focused first when the failure is a network one and downloads exist.

#include "common.h"
#include "../iptv/xtream.h"
#include "../platform/clock.h"
#include "../platform/log.h"

using namespace c2d;
using namespace iptv;

namespace {

    const int BUTTONS = 4;
    enum ButtonId {
        BTN_OFFLINE,
        BTN_RETRY,
        BTN_EDIT,
        BTN_PROFILES
    };

    class ConnectScreen : public Screen {
    public:
        ConnectScreen(App &a, const Profile &p) : Screen(a), profile(p) {
            ui::background(this);
            auto *t = ui::label(this, tr("connect.title", {p.name}), theme::TITLE, 0, 230, ui::Weight::SemiBold);
            t->setAlign(ui::Align::Center, theme::SCREEN_W);
            t->setMaxWidth(1400);
            auto *h = ui::label(this, screens::hostOf(p.server), theme::BODY, 0, 300, ui::Weight::Regular,
                                theme::textDim());
            h->setAlign(ui::Align::Center, theme::SCREEN_W);
            const char *names[] = {"connect.step_account", "connect.step_live"};
            float x = (theme::SCREEN_W - 760) / 2;
            for (int i = 0; i < STEPS; i++) {
                Step &s = steps[i];
                float y = 400 + (float) i * 92;
                s.bg = ui::box(this, FloatRect(x, y, 760, 76), theme::surface(), theme::RADIUS_SMALL);
                s.spinner = new ui::Spinner(14);
                s.spinner->setPosition(30, 31);
                s.bg->add(s.spinner);
                s.mark = ui::label(s.bg, "", theme::HEADING, 30, ui::Label::centerOffset(theme::HEADING, 76),
                                   ui::Weight::SemiBold);
                ui::label(s.bg, tr(names[i]), theme::BODY, 110, ui::Label::centerOffset(theme::BODY, 76),
                          ui::Weight::SemiBold);
                s.detail = ui::label(s.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, 76),
                                     ui::Weight::Regular, theme::textDim());
                s.detail->setAlign(ui::Align::Right, 730);
                s.detail->setMaxWidth(470);
                setStep(i, State::Pending, "");
            }
            message = ui::label(this, "", theme::BODY, 0, 760, ui::Weight::Regular, theme::danger());
            message->setAlign(ui::Align::Center, theme::SCREEN_W);
            message->setMaxWidth(1300);
            message->setMaxLines(3);
            const char *labels[BUTTONS] = {"connect.continue_offline", "common.retry", "connect.edit_profile",
                                           "connect.profiles"};
            const float bw = 330;
            const float gap = 24;
            float bx = (theme::SCREEN_W - (BUTTONS * bw + (BUTTONS - 1) * gap)) / 2;
            for (int i = 0; i < BUTTONS; i++) {
                buttons[i] = new ui::Button(tr(labels[i]), FloatRect(bx + (float) i * (bw + gap), 900, bw, 80),
                                            i == BTN_OFFLINE);
                buttons[i]->setVisibility(Visibility::Hidden);
                add(buttons[i]);
            }
            hints = screens::hintBar(this, {{ui::Glyph::Circle, tr("common.cancel")}});
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
                } else if (e.button == PadButton::Right && focus < BUTTONS - 1) {
                    focus++;
                } else if (e.button == PadButton::Cross && !e.repeat) {
                    switch (focus) {
                        case BTN_OFFLINE:
                            continueOffline();
                            break;
                        case BTN_RETRY:
                            start();
                            break;
                        case BTN_EDIT:
                            app.replaceAll(screens::makeProfiles(app));
                            app.push(screens::makeProfileEdit(app, profile));
                            break;
                        default:
                            app.replaceAll(screens::makeProfiles(app));
                            break;
                    }
                    return;
                } else if (e.button == PadButton::Circle) {
                    app.replaceAll(screens::makeProfiles(app));
                    return;
                }
                for (int i = 0; i < BUTTONS; i++) {
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
            offline = false;
            message->setText("");
            for (auto *b: buttons) {
                b->setVisibility(Visibility::Hidden);
            }
            hints->setHints({{ui::Glyph::Circle, tr("common.cancel")}});
            for (int i = 0; i < STEPS; i++) {
                setStep(i, State::Pending, "");
            }
            app.session() = Session();
            setStep(0, State::Running, tr("connect.signing_in"));
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
                    msg = tr("connect.expired_on", {msg, clockx::localDate(o.result.account.expiresAt)});
                } else if (o.result.status == AuthStatus::Network) {
                    msg = tr("connect.check_network", {msg});
                }
                fail(msg, o.result.status == AuthStatus::Network);
                return;
            }
            Session &s = app.session();
            s.profile = profile;
            s.account = o.result.account;
            s.httpsWarning = o.httpsWarning;
            app.library().setProfile(profile.id);
            app.vod().reset();   // Movies/Series of this profile load when first opened
            app.vod().setOffline(false);
            setStep(0, State::Done, o.result.account.expiresAt > 0
                                    ? tr("connect.active_until", {clockx::localDate(o.result.account.expiresAt)})
                                    : tr("account.active"));
            loadLive(false);
        }

        // the provider cannot be reached: the saved lists and the downloads
        void continueOffline() {
            LOG_I("connect", "continuing offline with profile %s", profile.id.c_str());
            failed = false;
            offline = true;
            for (auto *b: buttons) {
                b->setVisibility(Visibility::Hidden);
            }
            message->setText("");
            Session &s = app.session();
            s = Session();
            s.profile = profile;
            s.offline = true;
            app.library().setProfile(profile.id);
            app.vod().reset();
            app.vod().setOffline(true);
            loadLive(true);
        }

        void loadLive(bool cacheOnly) {
            setStep(1, State::Running, tr(cacheOnly ? "connect.loading_saved" : "connect.loading_channels"));
            token = app.xtream().loadLive(profile, APP_DATA_DIR, [this](XtreamService::LiveOutcome &o) {
                Session &s = app.session();
                if (o.ok) {
                    s.categories[0] = o.categories;
                    s.categoriesLoaded[0] = true;
                    s.live.assign(std::move(o.categories), std::move(o.channels));
                    s.live.fromCache = o.fromCache;
                    s.live.loadedAt = o.fromCache ? o.savedAt : clockx::unixNow();
                    s.liveLoaded = true;
                    s.liveNotice = o.fromCache ? o.message : "";
                    std::string detail = i18n::count("connect.categories", (long long) s.live.categories().size())
                                         + "  \xE2\x80\xA2  "
                                         + i18n::count("home.channels", (long long) s.live.channels().size());
                    setStep(1, State::Done, o.fromCache ? tr("connect.saved_list", {detail}) : detail);
                } else {
                    setStep(1, State::Failed, o.message);
                }
                s.connected = !offline;
                LOG_I("connect", "ready%s: live %d categories / %d channels", offline ? " (offline)" : "",
                      (int) s.categories[0].size(), (int) s.live.channels().size());
                app.replaceAll(screens::makeHome(app));
            }, cacheOnly);
        }

        void fail(const std::string &msg, bool networkProblem) {
            failed = true;
            message->setText(msg);
            // with downloads on the console and no connection, watching them is the likely wish
            dl::Totals t = app.downloads().totals();
            focus = networkProblem && t.completed > 0 ? BTN_OFFLINE : BTN_RETRY;
            for (int i = 0; i < BUTTONS; i++) {
                buttons[i]->setVisibility(Visibility::Visible);
                buttons[i]->setFocused(i == focus);
            }
            hints->setHints({{ui::Glyph::Cross, tr("common.select")}, {ui::Glyph::Circle, tr("connect.profiles")}});
        }

        static constexpr int STEPS = 2;

        Profile profile;
        Step steps[STEPS];
        ui::Label *message;
        ui::Button *buttons[BUTTONS];
        ui::HintBar *hints;
        CancelToken token;
        bool failed = false;
        bool offline = false;
        int focus = 0;
    };
}

namespace screens {
    Screen *makeConnect(App &app, const Profile &profile) {
        return new ConnectScreen(app, profile);
    }
}
