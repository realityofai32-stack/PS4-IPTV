// Onboarding, profile manager and profile editor.

#include "common.h"
#include "../core/url.h"
#include "../platform/clock.h"
#include "../platform/log.h"

using namespace c2d;
using namespace iptv;

namespace {

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class OnboardingScreen : public Screen {
    public:
        explicit OnboardingScreen(App &a) : Screen(a) {
            ui::background(this);
            float cx = theme::SCREEN_W / 2;
            auto *logo = ui::label(this, "PS4 IPTV", theme::DISPLAY, 0, 300, ui::Weight::SemiBold, theme::accent());
            logo->setAlign(ui::Align::Center, theme::SCREEN_W);
            auto *t = ui::label(this, "Welcome", theme::TITLE, 0, 400, ui::Weight::SemiBold);
            t->setAlign(ui::Align::Center, theme::SCREEN_W);
            auto *s = ui::label(this, "Add your IPTV provider to get started. You need the Xtream Codes server "
                                      "address, username and password from your provider.", theme::BODY, 0, 470,
                                ui::Weight::Regular, theme::textDim());
            s->setMaxWidth(980);
            s->setMaxLines(3);
            s->setAlign(ui::Align::Center, theme::SCREEN_W);
            button = new ui::Button("Add Xtream Profile", FloatRect(cx - 230, 620, 460, 88), true);
            button->setFocused(true);
            add(button);
            screens::hintBar(this, {{ui::Glyph::Cross, "Add profile"}, {ui::Glyph::Circle, "Exit"}});
        }

        const char *name() const override { return "onboarding"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Cross && !e.repeat) {
                app.push(screens::makeProfileEdit(app, Profile()));
            } else if (e.button == PadButton::Circle) {
                app.push(screens::makeDialog(app, "Exit PS4 IPTV?", "", {"Cancel", "Exit"}, [this](int c) {
                    if (c == 1) {
                        app.quit();
                    }
                }));
            }
        }

    private:
        ui::Button *button;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class ProfilesScreen : public Screen, public ui::ListView::Adapter {
    public:
        explicit ProfilesScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, "Profiles", "Choose the IPTV provider to watch, or add a new one.");
            list = new ui::ListView(FloatRect(theme::SAFE_X, 220, 900, 720), 112, 16, this);
            add(list);
            detail = ui::box(this, FloatRect(1060, 220, 764, 720), theme::surface(), theme::RADIUS);
            detailTitle = ui::label(detail, "", theme::HEADING, 48, 44, ui::Weight::SemiBold);
            detailTitle->setMaxWidth(668);
            detailBody = ui::label(detail, "", theme::BODY, 48, 112, ui::Weight::Regular, theme::textDim());
            detailBody->setMaxWidth(668);
            detailBody->setMaxLines(12);
            hints = screens::hintBar(this, {});
            refresh();
        }

        const char *name() const override { return "profiles"; }

        void onResume() override {
            list->reload();
            refresh();
        }

        // ListView::Adapter
        int count() override { return (int) app.profiles().profiles().size() + 1; }

        C2DObject *createRow(float w, float h) override {
            auto *row = ui::box(nullptr, FloatRect(0, 0, w, h), theme::surface(), theme::RADIUS);
            auto *title = ui::label(row, "", theme::HEADING, 36, 18, ui::Weight::SemiBold);
            title->setMaxWidth(w - 220);
            auto *sub = ui::label(row, "", theme::LABEL, 36, 64, ui::Weight::Regular, theme::textDim());
            sub->setMaxWidth(w - 72);
            auto *badge = ui::label(row, "", theme::LABEL, 0, 22, ui::Weight::SemiBold, theme::success());
            badge->setAlign(ui::Align::Right, w - 36);
            rows.push_back({row, title, sub, badge});
            return row;
        }

        void bindRow(C2DObject *obj, int index, bool selected, bool focused) override {
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                const auto &profiles = app.profiles().profiles();
                bool add = index >= (int) profiles.size();
                r.bg->setFillColor(focused ? theme::surfaceFocus() : theme::surface());
                r.bg->setOutlineColor(theme::accent());
                r.bg->setOutlineThickness(focused ? theme::FOCUS_BORDER : 0);
                (void) selected;
                if (add) {
                    r.title->setText("+  Add profile");
                    r.title->setColor(theme::accent());
                    r.sub->setText("Xtream Codes server, username and password");
                    r.badge->setText("");
                } else {
                    const Profile &p = profiles[(size_t) index];
                    r.title->setText(p.name);
                    r.title->setColor(theme::text());
                    r.sub->setText(screens::hostOf(p.server) + (p.lastStatus.empty() ? "" : "   \xE2\x80\xA2   "
                                                                                            + p.lastStatus));
                    bool active = p.id == app.profiles().activeId();
                    r.badge->setText(active ? "ACTIVE" : "");
                }
            }
        }

        void handleInput(const InputEvent &e) override {
            const auto &profiles = app.profiles().profiles();
            int index = list->selected();
            bool onAdd = index >= (int) profiles.size();
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    break;
                case PadButton::Down:
                    list->moveSelection(1);
                    break;
                case PadButton::Cross:
                    if (e.repeat) {
                        return;
                    }
                    if (onAdd) {
                        app.push(screens::makeProfileEdit(app, Profile()));
                    } else {
                        app.profiles().setActive(profiles[(size_t) index].id);
                        app.profiles().save();
                        app.replaceAll(screens::makeConnect(app, profiles[(size_t) index]));
                    }
                    return;
                case PadButton::Square:
                    if (!onAdd && !e.repeat) {
                        app.push(screens::makeProfileEdit(app, profiles[(size_t) index]));
                    }
                    return;
                case PadButton::Options:
                    app.push(screens::makeProfileEdit(app, Profile()));
                    return;
                case PadButton::Triangle:
                    if (!onAdd && !e.repeat) {
                        confirmDelete(profiles[(size_t) index]);
                    }
                    return;
                case PadButton::Circle:
                    if (app.session().connected) {
                        app.replaceAll(screens::makeHome(app));
                    } else {
                        app.push(screens::makeDialog(app, "Exit PS4 IPTV?", "", {"Cancel", "Exit"}, [this](int c) {
                            if (c == 1) {
                                app.quit();
                            }
                        }));
                    }
                    return;
                default:
                    return;
            }
            refresh();
        }

    private:
        struct Row {
            RectangleShape *bg;
            ui::Label *title;
            ui::Label *sub;
            ui::Label *badge;
        };

        void confirmDelete(const Profile &p) {
            std::string id = p.id;
            app.push(screens::makeDialog(app, "Delete profile?",
                                         "\"" + p.name + "\" and its saved login will be removed from this console.",
                                         {"Cancel", "Delete"}, [this, id](int c) {
                        if (c != 1) {
                            return;
                        }
                        bool wasActive = app.session().connected && app.session().profile.id == id;
                        app.profiles().remove(id);
                        std::string err;
                        if (!app.profiles().save(&err)) {
                            LOG_E("profiles", "save failed: %s", err.c_str());
                            app.toast("Could not save profiles", ToastKind::Error);
                        }
                        if (wasActive) {
                            app.session() = Session();
                        }
                        if (app.profiles().profiles().empty()) {
                            app.replaceAll(screens::makeOnboarding(app));
                            return;
                        }
                        list->reload();
                        refresh();
                        app.toast("Profile deleted", ToastKind::Success);
                    }, true));
        }

        void refresh() {
            const auto &profiles = app.profiles().profiles();
            int index = list->selected();
            if (index >= (int) profiles.size()) {
                detailTitle->setText("Add a profile");
                detailBody->setText("Enter the server address, username and password your IPTV provider gave you. "
                                    "Use Test Connection to check them before saving.");
                hints->setHints({{ui::Glyph::Cross, "Add"}, {ui::Glyph::Circle, "Back"}});
                return;
            }
            const Profile &p = profiles[(size_t) index];
            detailTitle->setText(p.name);
            std::string body = "Server\n" + screens::hostOf(p.server) + "\n\nUsername\n" + p.username
                               + "\n\nPassword\n" + screens::mask(p.password);
            if (!p.lastStatus.empty()) {
                body += "\n\nLast connection\n" + p.lastStatus;
                if (p.lastUsedAt > 0) {
                    body += " (" + clockx::localDate(p.lastUsedAt) + ")";
                }
            }
            detailBody->setText(body);
            hints->setHints({{ui::Glyph::Cross, "Connect"}, {ui::Glyph::Square, "Edit"},
                             {ui::Glyph::Triangle, "Delete"}, {ui::Glyph::Options, "Add"},
                             {ui::Glyph::Circle, "Back"}});
        }

        ui::ListView *list;
        RectangleShape *detail;
        ui::Label *detailTitle;
        ui::Label *detailBody;
        ui::HintBar *hints;
        std::vector<Row> rows;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class ProfileEditScreen : public Screen {
    public:
        ProfileEditScreen(App &a, const Profile &p) : Screen(a), profile(p) {
            isNew = p.id.empty();
            if (!isNew) {
                serverInput = p.server;
            }
            ui::background(this);
            screens::header(this, isNew ? "Add Xtream profile" : "Edit profile",
                            "Server address, username and password from your IPTV provider.");
            const char *captions[] = {"Profile name", "Server URL", "Username", "Password"};
            for (int i = 0; i < 4; i++) {
                fields[i] = new screens::FieldRow(FloatRect(theme::SAFE_X, 230 + (float) i * 104, 1000, 88),
                                                  captions[i]);
                add(fields[i]);
            }
            const char *labels[] = {"Test Connection", isNew ? "Save & Connect" : "Save & Connect", "Cancel"};
            float x = theme::SAFE_X;
            for (int i = 0; i < 3; i++) {
                float w = i == 2 ? 220.0f : 340.0f;
                buttons[i] = new ui::Button(labels[i], FloatRect(x, 690, w, 84), i == 1);
                add(buttons[i]);
                x += w + 24;
            }
            status = ui::box(this, FloatRect(1160, 230, 664, 544), theme::surface(), theme::RADIUS);
            statusTitle = ui::label(status, "Connection", theme::HEADING, 40, 36, ui::Weight::SemiBold);
            statusBody = ui::label(status, "", theme::BODY, 40, 100, ui::Weight::Regular, theme::textDim());
            statusBody->setMaxWidth(584);
            statusBody->setMaxLines(11);
            spinner = new ui::Spinner(18);
            spinner->setPosition(40, 110);
            spinner->setVisibility(Visibility::Hidden);
            status->add(spinner);
            screens::hintBar(this, {{ui::Glyph::Cross, "Edit / select"}, {ui::Glyph::Circle, "Cancel"}});
            setStatus(isNew ? "Fill in the fields, then use Test Connection." : "Test the connection after changes.",
                      theme::textDim());
            refresh();
        }

        ~ProfileEditScreen() override {
            testToken.cancel();
        }

        const char *name() const override { return "profile-edit"; }

        bool animating() const override { return false; }

        void tick(double now) override {
            if (testing && spinner->tick(now)) {
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    if (focus >= 4) {
                        focus = 3;
                    } else if (focus > 0) {
                        focus--;
                    }
                    break;
                case PadButton::Down:
                    if (focus < 4) {
                        focus++;
                    }
                    break;
                case PadButton::Left:
                    if (focus > 4) {
                        focus--;
                    }
                    break;
                case PadButton::Right:
                    if (focus >= 4 && focus < 6) {
                        focus++;
                    }
                    break;
                case PadButton::Cross:
                    if (!e.repeat) {
                        activate();
                    }
                    return;
                case PadButton::Circle:
                    app.pop();
                    return;
                default:
                    return;
            }
            refresh();
        }

    private:

        void activate() {
            switch (focus) {
                case 0:
                    edit("Profile name", profile.name, false, [this](const std::string &v) { profile.name = v; });
                    break;
                case 1:
                    edit("Server URL", serverInput, false, [this](const std::string &v) { serverInput = v; });
                    break;
                case 2:
                    edit("Username", profile.username, false, [this](const std::string &v) { profile.username = v; });
                    break;
                case 3:
                    edit("Password", profile.password, true, [this](const std::string &v) { profile.password = v; });
                    break;
                case 4:
                    test(false);
                    break;
                case 5:
                    test(true);
                    break;
                default:
                    app.pop();
                    break;
            }
        }

        void edit(const char *title, const std::string &value, bool secret, std::function<void(const std::string &)> set) {
            app.push(screens::makeKeyboard(app, title, value, secret, [this, set](const std::string &v) {
                set(v);
                tested = false;
                refresh();
            }));
        }

        // validates the form into `profile`; returns an error message or ""
        std::string validate() {
            url::Server s = url::normalizeServer(serverInput);
            if (!s.ok) {
                return s.error;
            }
            if (url::trim(profile.username).empty()) {
                return "Enter the username";
            }
            if (profile.password.empty()) {
                return "Enter the password";
            }
            profile.server = s.base;
            profile.username = url::trim(profile.username);
            if (url::trim(profile.name).empty()) {
                profile.name = s.displayHost;
            }
            return "";
        }

        void test(bool thenSave) {
            if (testing) {
                return;
            }
            std::string err = validate();
            if (!err.empty()) {
                setStatus(err, theme::danger());
                refresh();
                return;
            }
            if (thenSave && tested) {
                save();
                return;
            }
            testing = true;
            spinner->setVisibility(Visibility::Visible);
            setStatus("", theme::textDim());
            statusTitle->setText("Testing connection" "\xE2\x80\xA6");
            LOG_I("profiles", "testing connection to %s", screens::hostOf(profile.server).c_str());
            testToken = app.xtream().authenticate(profile, [this, thenSave](const XtreamService::AuthOutcome &o) {
                testing = false;
                spinner->setVisibility(Visibility::Hidden);
                statusTitle->setText("Connection");
                if (o.result.status == AuthStatus::Ok) {
                    tested = true;
                    std::string body = "Connected\n\n" + screens::accountSummary(o.result.account);
                    if (o.httpsWarning) {
                        body += "\n\nThis server uses HTTPS. This build currently supports HTTP streams only, so "
                                "playback may not work.";
                    }
                    setStatus(body, o.httpsWarning ? theme::warning() : theme::success());
                    if (thenSave) {
                        save();
                    }
                } else {
                    tested = false;
                    std::string body = o.message;
                    if (o.result.status == AuthStatus::Expired && o.result.account.expiresAt > 0) {
                        body += "\nExpired on " + clockx::localDate(o.result.account.expiresAt);
                    }
                    if (!o.result.account.message.empty()) {
                        body += "\n\nProvider message: " + o.result.account.message;
                    }
                    if (thenSave) {
                        body += "\n\nNot saved. Fix the details, or choose Save & Connect again after Test "
                                "Connection succeeds.";
                    }
                    setStatus(body, theme::danger());
                }
                refresh();
            });
            refresh();
        }

        void save() {
            Profile p = profile;
            std::string id = app.profiles().upsert(p, clockx::unixNow());
            app.profiles().setActive(id);
            std::string err;
            if (!app.profiles().save(&err)) {
                LOG_E("profiles", "save failed: %s", err.c_str());
                setStatus("Could not save the profile on this console.", theme::danger());
                return;
            }
            LOG_I("profiles", "profile %s saved (%s)", id.c_str(), screens::hostOf(p.server).c_str());
            app.replaceAll(screens::makeConnect(app, *app.profiles().find(id)));
        }

        void setStatus(const std::string &text, const Color &color) {
            statusBody->setText(text);
            statusBody->setColor(color);
        }

        void refresh() {
            fields[0]->setValue(profile.name.empty() ? "Optional, e.g. My IPTV" : profile.name, profile.name.empty());
            fields[1]->setValue(serverInput.empty() ? "http://example.com:8080" : serverInput, serverInput.empty());
            fields[2]->setValue(profile.username.empty() ? "Required" : profile.username, profile.username.empty());
            fields[3]->setValue(profile.password.empty() ? "Required" : screens::mask(profile.password),
                                profile.password.empty());
            for (int i = 0; i < 4; i++) {
                fields[i]->setFocused(focus == i);
            }
            for (int i = 0; i < 3; i++) {
                buttons[i]->setFocused(focus == 4 + i);
                buttons[i]->setEnabled(!testing || i == 2);
            }
        }

        Profile profile;
        std::string serverInput;
        bool isNew;
        bool tested = false;
        bool testing = false;
        int focus = 0;
        screens::FieldRow *fields[4];
        ui::Button *buttons[3];
        RectangleShape *status;
        ui::Label *statusTitle;
        ui::Label *statusBody;
        ui::Spinner *spinner;
        CancelToken testToken;
    };
}

namespace screens {
    Screen *makeOnboarding(App &app) {
        return new OnboardingScreen(app);
    }

    Screen *makeProfiles(App &app) {
        return new ProfilesScreen(app);
    }

    Screen *makeProfileEdit(App &app, const Profile &profile) {
        return new ProfileEditScreen(app, profile);
    }
}
