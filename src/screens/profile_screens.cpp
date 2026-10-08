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
            auto *logo = ui::label(this, tr("app.name"), theme::DISPLAY, 0, 300, ui::Weight::SemiBold, theme::accent());
            logo->setAlign(ui::Align::Center, theme::SCREEN_W);
            auto *t = ui::label(this, tr("onboarding.welcome"), theme::TITLE, 0, 400, ui::Weight::SemiBold);
            t->setAlign(ui::Align::Center, theme::SCREEN_W);
            auto *s = ui::label(this, tr("onboarding.text"), theme::BODY, 0, 470,
                                ui::Weight::Regular, theme::textDim());
            s->setMaxWidth(980);
            s->setMaxLines(3);
            s->setAlign(ui::Align::Center, theme::SCREEN_W);
            // downloads stay playable when every profile was deleted: they are reachable from here too
            hasDownloads = !app.downloads().items().empty();
            float bw = 460;
            float bx = hasDownloads ? cx - bw - 12 : cx - bw / 2;
            button = new ui::Button(tr("onboarding.add_profile"), FloatRect(bx, 620, bw, 88), true);
            add(button);
            downloadsButton = new ui::Button(tr("home.downloads"), FloatRect(cx + 12, 620, bw, 88));
            downloadsButton->setVisibility(hasDownloads ? Visibility::Visible : Visibility::Hidden);
            add(downloadsButton);
            screens::hintBar(this, {{ui::Glyph::Cross, tr("common.select")}, {ui::Glyph::Circle, tr("common.exit")}});
            refresh();
        }

        const char *name() const override { return "onboarding"; }

        void handleInput(const InputEvent &e) override {
            if ((e.button == PadButton::Left || e.button == PadButton::Right) && hasDownloads) {
                focus = e.button == PadButton::Right ? 1 : 0;
                refresh();
            } else if (e.button == PadButton::Cross && !e.repeat) {
                if (focus == 1) {
                    app.push(screens::makeDownloads(app));
                } else {
                    app.push(screens::makeProfileEdit(app, Profile()));
                }
            } else if (e.button == PadButton::Circle) {
                app.push(screens::makeDialog(app, tr("app.exit_title"), "", {tr("common.cancel"), tr("common.exit")},
                                             [this](int c) {
                                                 if (c == 1) {
                                                     app.quit();
                                                 }
                                             }));
            }
        }

    private:
        void refresh() {
            button->setFocused(focus == 0);
            downloadsButton->setFocused(focus == 1);
        }

        ui::Button *button;
        ui::Button *downloadsButton;
        bool hasDownloads = false;
        int focus = 0;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class ProfilesScreen : public Screen, public ui::ListView::Adapter {
    public:
        explicit ProfilesScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, tr("profiles.title"), tr("profiles.subtitle"));
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
                    r.title->setText("+  " + tr("profiles.add"));
                    r.title->setColor(theme::accent());
                    r.sub->setText(tr("profiles.add_sub"));
                    r.badge->setText("");
                } else {
                    const Profile &p = profiles[(size_t) index];
                    r.title->setText(p.name);
                    r.title->setColor(theme::text());
                    r.sub->setText(screens::hostOf(p.server) + (p.lastStatus.empty() ? "" : "   \xE2\x80\xA2   "
                                                                                            + p.lastStatus));
                    bool active = p.id == app.profiles().activeId();
                    r.badge->setText(active ? tr("profiles.active_badge") : "");
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
                    if (app.session().connected || app.session().offline) {
                        app.replaceAll(screens::makeHome(app));
                    } else {
                        app.push(screens::makeDialog(app, tr("app.exit_title"), "",
                                                     {tr("common.cancel"), tr("common.exit")}, [this](int c) {
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
            app.push(screens::makeDialog(app, tr("profiles.delete_title"), tr("profiles.delete_text", {p.name}),
                                         {tr("common.cancel"), tr("common.delete")}, [this, id](int c) {
                        if (c != 1) {
                            return;
                        }
                        bool wasActive = (app.session().connected || app.session().offline) && app.session().profile.id == id;
                        app.profiles().remove(id);
                        std::string err;
                        if (!app.profiles().save(&err)) {
                            LOG_E("profiles", "save failed: %s", err.c_str());
                            app.toast(tr("profiles.save_failed"), ToastKind::Error);
                        }
                        app.syncDownloadProfiles();   // completed downloads stay playable; queued ones stop
                        if (wasActive) {
                            app.session() = Session();
                        }
                        if (app.profiles().profiles().empty()) {
                            app.replaceAll(screens::makeOnboarding(app));
                            return;
                        }
                        list->reload();
                        refresh();
                        app.toast(tr("profiles.deleted"), ToastKind::Success);
                    }, true));
        }

        void refresh() {
            const auto &profiles = app.profiles().profiles();
            int index = list->selected();
            if (index >= (int) profiles.size()) {
                detailTitle->setText(tr("profiles.add_title"));
                detailBody->setText(tr("profiles.add_text"));
                hints->setHints({{ui::Glyph::Cross, tr("common.add")}, {ui::Glyph::Circle, tr("common.back")}});
                return;
            }
            const Profile &p = profiles[(size_t) index];
            detailTitle->setText(p.name);
            std::string body = tr("profile.server") + "\n" + screens::hostOf(p.server) + "\n\n" + tr("profile.username")
                               + "\n" + p.username + "\n\n" + tr("profile.password") + "\n" + screens::mask(p.password);
            if (!p.lastStatus.empty()) {
                body += "\n\n" + tr("profiles.last_connection") + "\n" + p.lastStatus;
                if (p.lastUsedAt > 0) {
                    body += " (" + clockx::localDate(p.lastUsedAt) + ")";
                }
            }
            detailBody->setText(body);
            hints->setHints({{ui::Glyph::Cross, tr("profiles.connect")}, {ui::Glyph::Square, tr("common.edit")},
                             {ui::Glyph::Triangle, tr("common.delete")}, {ui::Glyph::Options, tr("common.add")},
                             {ui::Glyph::Circle, tr("common.back")}});
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
            screens::header(this, tr(isNew ? "profile.add_title" : "profile.edit_title"), tr("profile.subtitle"));
            const char *captions[] = {"profile.name", "profile.server_url", "profile.username", "profile.password"};
            for (int i = 0; i < 4; i++) {
                fields[i] = new screens::FieldRow(FloatRect(theme::SAFE_X, 230 + (float) i * 104, 1000, 88),
                                                  tr(captions[i]));
                add(fields[i]);
            }
            const char *labels[] = {"profile.test", "profile.save_connect", "common.cancel"};
            float x = theme::SAFE_X;
            for (int i = 0; i < 3; i++) {
                float w = i == 2 ? 220.0f : 340.0f;
                buttons[i] = new ui::Button(tr(labels[i]), FloatRect(x, 690, w, 84), i == 1);
                add(buttons[i]);
                x += w + 24;
            }
            status = ui::box(this, FloatRect(1160, 230, 664, 544), theme::surface(), theme::RADIUS);
            statusTitle = ui::label(status, tr("profile.connection"), theme::HEADING, 40, 36, ui::Weight::SemiBold);
            statusBody = ui::label(status, "", theme::BODY, 40, 100, ui::Weight::Regular, theme::textDim());
            statusBody->setMaxWidth(584);
            statusBody->setMaxLines(11);
            spinner = new ui::Spinner(18);
            spinner->setPosition(40, 110);
            spinner->setVisibility(Visibility::Hidden);
            status->add(spinner);
            screens::hintBar(this, {{ui::Glyph::Cross, tr("profile.hint_edit")}, {ui::Glyph::Circle, tr("common.cancel")}});
            setStatus(tr(isNew ? "profile.status_new" : "profile.status_edit"), theme::textDim());
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
                    edit(tr("profile.name"), profile.name, false, [this](const std::string &v) { profile.name = v; });
                    break;
                case 1:
                    edit(tr("profile.server_url"), serverInput, false, [this](const std::string &v) { serverInput = v; });
                    break;
                case 2:
                    edit(tr("profile.username"), profile.username, false, [this](const std::string &v) { profile.username = v; });
                    break;
                case 3:
                    edit(tr("profile.password"), profile.password, true, [this](const std::string &v) { profile.password = v; });
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

        void edit(const std::string &title, const std::string &value, bool secret, std::function<void(const std::string &)> set) {
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
                return tr("profile.error.username");
            }
            if (profile.password.empty()) {
                return tr("profile.error.password");
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
            statusTitle->setText(tr("profile.testing"));
            LOG_I("profiles", "testing connection to %s", screens::hostOf(profile.server).c_str());
            testToken = app.xtream().authenticate(profile, [this, thenSave](const XtreamService::AuthOutcome &o) {
                testing = false;
                spinner->setVisibility(Visibility::Hidden);
                statusTitle->setText(tr("profile.connection"));
                if (o.result.status == AuthStatus::Ok) {
                    tested = true;
                    std::string body = tr("auth.ok") + "\n\n" + screens::accountSummary(o.result.account);
                    if (o.httpsWarning) {
                        body += "\n\n" + tr("profile.https_warning");
                    }
                    setStatus(body, o.httpsWarning ? theme::warning() : theme::success());
                    if (thenSave) {
                        save();
                    }
                } else {
                    tested = false;
                    std::string body = o.message;
                    if (o.result.status == AuthStatus::Expired && o.result.account.expiresAt > 0) {
                        body += "\n" + tr("profile.expired_on", {clockx::localDate(o.result.account.expiresAt)});
                    }
                    if (!o.result.account.message.empty()) {
                        body += "\n\n" + tr("profile.provider_message", {o.result.account.message});
                    }
                    if (thenSave) {
                        body += "\n\n" + tr("profile.not_saved");
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
                setStatus(tr("profile.save_failed"), theme::danger());
                return;
            }
            app.syncDownloadProfiles();
            LOG_I("profiles", "profile %s saved (%s)", id.c_str(), screens::hostOf(p.server).c_str());
            app.replaceAll(screens::makeConnect(app, *app.profiles().find(id)));
        }

        void setStatus(const std::string &text, const Color &color) {
            statusBody->setText(text);
            statusBody->setColor(color);
        }

        void refresh() {
            fields[0]->setValue(profile.name.empty() ? tr("profile.name_placeholder") : profile.name, profile.name.empty());
            fields[1]->setValue(serverInput.empty() ? "http://example.com:8080" : serverInput, serverInput.empty());
            fields[2]->setValue(profile.username.empty() ? tr("profile.required") : profile.username, profile.username.empty());
            fields[3]->setValue(profile.password.empty() ? tr("profile.required") : screens::mask(profile.password),
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
