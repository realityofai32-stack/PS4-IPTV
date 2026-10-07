// Modal on-screen keyboard (custom; SceImeDialog is not used).

#include "common.h"
#include "../ui/keyboard_model.h"

using namespace c2d;

namespace {

    const float PANEL_W = 1440;
    const float PANEL_H = 820;
    const float KEY_UNIT = 100;
    const float KEY_GAP = 10;
    const float KEY_H = 74;

    class KeyboardScreen : public Screen {

    public:

        KeyboardScreen(App &a, const std::string &title, const std::string &initial, bool isPassword,
                       std::function<void(const std::string &)> done)
                : Screen(a, true), model(initial, 512), password(isPassword), onDone(std::move(done)) {
            float px = (theme::SCREEN_W - PANEL_W) / 2;
            float py = (theme::SCREEN_H - PANEL_H) / 2 - 20;
            auto *panel = ui::box(this, FloatRect(px, py, PANEL_W, PANEL_H), theme::surface(), 24);
            ui::label(panel, title, theme::HEADING, 60, 40, ui::Weight::SemiBold);

            auto *field = ui::box(panel, FloatRect(60, 104, PANEL_W - 120, 84), theme::bgTop(), theme::RADIUS_SMALL);
            field->setOutlineColor(theme::accent());
            field->setOutlineThickness(2);
            preview = ui::label(field, "", theme::HEADING, 24, ui::Label::centerOffset(theme::HEADING, 84));
            preview->setMaxWidth(PANEL_W - 120 - 48);

            float gridW = KeyboardModel::COLUMNS * KEY_UNIT + (KeyboardModel::COLUMNS - 1) * KEY_GAP;
            float gx = (PANEL_W - gridW) / 2;
            float gy = 220;
            const auto &rows = model.rows();
            for (int r = 0; r < (int) rows.size(); r++) {
                std::vector<KeyView> line;
                for (int c = 0; c < (int) rows[(size_t) r].size(); c++) {
                    const Key &k = rows[(size_t) r][(size_t) c];
                    float x = gx + (float) model.keyStart(r, c) * (KEY_UNIT + KEY_GAP);
                    float w = (float) k.span * KEY_UNIT + (float) (k.span - 1) * KEY_GAP;
                    KeyView v;
                    v.bg = ui::box(panel, FloatRect(x, gy + (float) r * (KEY_H + KEY_GAP), w, KEY_H),
                                   theme::surfaceRaised(), 12);
                    bool special = k.action != KeyAction::Char;
                    v.label = ui::label(v.bg, "", special ? theme::LABEL : theme::HEADING, 0,
                                        ui::Label::centerOffset(special ? theme::LABEL : theme::HEADING, KEY_H),
                                        ui::Weight::SemiBold);
                    v.label->setAlign(ui::Align::Center, w);
                    line.push_back(v);
                }
                keys.push_back(line);
            }
            screens::hintBar(this, {{ui::Glyph::Cross, "Type"}, {ui::Glyph::Square, "Backspace"},
                                    {ui::Glyph::Triangle, "Space"}, {ui::Glyph::L2, "Shift"},
                                    {ui::Glyph::R2, "Done"}, {ui::Glyph::Circle, "Cancel"}});
            refresh();
        }

        const char *name() const override { return "keyboard"; }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    model.move(0, -1);
                    break;
                case PadButton::Down:
                    model.move(0, 1);
                    break;
                case PadButton::Left:
                    model.move(-1, 0);
                    break;
                case PadButton::Right:
                    model.move(1, 0);
                    break;
                case PadButton::Cross:
                    if (e.repeat) {
                        return;
                    }
                    switch (model.press()) {
                        case KeyboardModel::Result::Ok:
                            finish(true);
                            return;
                        case KeyboardModel::Result::Cancel:
                            finish(false);
                            return;
                        default:
                            break;
                    }
                    break;
                case PadButton::Square:
                    model.backspace();
                    break;
                case PadButton::Triangle:
                    model.space();
                    break;
                case PadButton::L2:
                    if (!e.repeat) {
                        model.toggleShift();
                    }
                    break;
                case PadButton::R2:
                case PadButton::Options:
                    if (!e.repeat) {
                        finish(true);
                    }
                    return;
                case PadButton::Circle:
                    finish(false);
                    return;
                default:
                    return;
            }
            refresh();
        }

    private:

        struct KeyView {
            RectangleShape *bg;
            ui::Label *label;
        };

        void finish(bool ok) {
            // pop first: if the callback opens another screen, it must land above the caller
            app.pop();
            if (ok && onDone) {
                onDone(model.text());
            }
        }

        void refresh() {
            std::string shown = password ? screens::mask(model.text()) : model.text();
            preview->setText(shown + "|");
            const auto &rows = model.rows();
            for (int r = 0; r < (int) rows.size(); r++) {
                for (int c = 0; c < (int) rows[(size_t) r].size(); c++) {
                    const Key &k = rows[(size_t) r][(size_t) c];
                    KeyView &v = keys[(size_t) r][(size_t) c];
                    bool focused = r == model.focusRow() && c == model.focusCol();
                    bool active = k.action == KeyAction::Shift && model.shift();
                    v.label->setText(model.label(k));
                    Color bg = focused ? theme::accent() : active ? theme::accentDark()
                                                                  : k.action == KeyAction::Ok ? Color(30, 74, 140)
                                                                  : k.action == KeyAction::Char ? theme::surfaceRaised()
                                                                  : theme::surfaceFocus();
                    v.bg->setFillColor(bg);
                    v.bg->setOutlineColor(Color::White);
                    v.bg->setOutlineThickness(focused ? 3 : 0);
                }
            }
        }

        KeyboardModel model;
        bool password;
        std::function<void(const std::string &)> onDone;
        ui::Label *preview = nullptr;
        std::vector<std::vector<KeyView>> keys;
    };

    class DialogScreen : public Screen {

    public:

        DialogScreen(App &a, const std::string &title, const std::string &message,
                     const std::vector<std::string> &buttons, std::function<void(int)> choice, bool destructive)
                : Screen(a, true), onChoice(std::move(choice)) {
            const float w = 980;
            auto *msg = new ui::Label(message, theme::BODY, ui::Weight::Regular, theme::textDim());
            msg->setMaxWidth(w - 120);
            msg->setMaxLines(8);
            float msgH = message.empty() ? 0 : msg->height();
            float h = 120 + msgH + 40 + 76 + 56;
            auto *panel = ui::box(this, FloatRect((theme::SCREEN_W - w) / 2, (theme::SCREEN_H - h) / 2, w, h),
                                  theme::surface(), 24);
            ui::label(panel, title, theme::HEADING, 60, 48, ui::Weight::SemiBold);
            msg->setPosition(60, 112);
            panel->add(msg);
            float bw = 260;
            float total = (float) buttons.size() * bw + (float) (buttons.size() - 1) * 24;
            float x = w - 60 - total;
            for (size_t i = 0; i < buttons.size(); i++) {
                auto *b = new ui::Button(buttons[i], FloatRect(x, h - 56 - 76, bw, 76),
                                         destructive && i == buttons.size() - 1);
                panel->add(b);
                btns.push_back(b);
                x += bw + 24;
            }
            screens::hintBar(this, {{ui::Glyph::Cross, "Select"}, {ui::Glyph::Circle, "Back"}});
            refresh();
        }

        const char *name() const override { return "dialog"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Left && focus > 0) {
                focus--;
            } else if (e.button == PadButton::Right && focus + 1 < (int) btns.size()) {
                focus++;
            } else if (e.button == PadButton::Cross && !e.repeat) {
                int chosen = focus;
                auto cb = onChoice;
                app.pop();
                if (cb) {
                    cb(chosen);
                }
                return;
            } else if (e.button == PadButton::Circle) {
                auto cb = onChoice;
                app.pop();
                if (cb) {
                    cb(-1);
                }
                return;
            }
            refresh();
        }

    private:

        void refresh() {
            for (size_t i = 0; i < btns.size(); i++) {
                btns[i]->setFocused((int) i == focus);
            }
        }

        std::function<void(int)> onChoice;
        std::vector<ui::Button *> btns;
        int focus = 0;
    };
}

namespace {

    // Options menus: a list in a panel on the right, over a dimmed screen
    class MenuScreen : public Screen, public ui::ListView::Adapter {

    public:

        MenuScreen(App &a, const std::string &title, std::vector<std::string> opts, int checkedIndex,
                   std::function<void(int)> choice, std::vector<std::string> extra)
                : Screen(a, true), options(std::move(opts)), details(std::move(extra)), checked(checkedIndex),
                  onChoice(std::move(choice)) {
            const float w = 680;
            auto *panel = ui::box(this, FloatRect(theme::SCREEN_W - w, 0, w, theme::SCREEN_H), Color(14, 18, 26, 245), 0);
            auto *t = ui::label(panel, title, theme::TITLE, 48, 64, ui::Weight::SemiBold);
            t->setMaxWidth(w - 96);
            list = new ui::ListView(FloatRect(40, 170, w - 70, 760), 84, 8, this);
            panel->add(list);
            auto *hints = new ui::HintBar();
            hints->setPosition(48, theme::SCREEN_H - theme::SAFE_Y - 40);
            hints->setHints({{ui::Glyph::Cross, "Select"}, {ui::Glyph::Circle, "Back"}});
            panel->add(hints);
            list->setSelected(checked >= 0 ? checked : 0);
        }

        const char *name() const override { return "menu"; }

        int count() override { return (int) options.size(); }

        C2DObject *createRow(float w, float h) override {
            Row r;
            r.bg = ui::box(nullptr, FloatRect(0, 0, w, h), Color::Transparent, theme::RADIUS_SMALL);
            r.check = ui::label(r.bg, "", theme::BODY, 20, ui::Label::centerOffset(theme::BODY, h), ui::Weight::SemiBold,
                                theme::accent());
            r.name = ui::label(r.bg, "", theme::BODY, 64, ui::Label::centerOffset(theme::BODY, h));
            r.name->setMaxWidth(w - 64 - 24);
            r.detail = ui::label(r.bg, "", theme::CAPTION, 64, ui::Label::centerOffset(theme::BODY, h) + 22,
                                 ui::Weight::Regular, theme::textMuted());
            r.detail->setMaxWidth(w - 64 - 24);
            rows.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int i, bool, bool focused) override {
            bool hasDetail = i < (int) details.size() && !details[(size_t) i].empty();
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                r.check->setText(i == checked ? "\xE2\x9C\x93" : "");
                r.name->setText(options[(size_t) i]);
                r.name->setPosition(64, ui::Label::centerOffset(theme::BODY, 84) - (hasDetail ? 12 : 0));
                r.detail->setText(hasDetail ? details[(size_t) i] : "");
                r.name->setColor(focused ? Color::White : theme::text());
                r.bg->setFillColor(focused ? theme::rowFocus() : Color::Transparent);
                r.bg->setOutlineColor(theme::accent());
                r.bg->setOutlineThickness(focused ? 3 : 0);
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    return;
                case PadButton::Down:
                    list->moveSelection(1);
                    return;
                case PadButton::Cross:
                case PadButton::Circle:
                case PadButton::Options: {
                    if (e.repeat) {
                        return;
                    }
                    int chosen = e.button == PadButton::Cross ? list->selected() : -1;
                    auto cb = onChoice;
                    app.pop();   // first: a callback may open another screen
                    if (cb) {
                        cb(chosen);
                    }
                    return;
                }
                default:
                    return;
            }
        }

    private:

        struct Row {
            RectangleShape *bg;
            ui::Label *check;
            ui::Label *name;
            ui::Label *detail;
        };

        std::vector<std::string> options;
        std::vector<std::string> details;
        int checked;
        std::function<void(int)> onChoice;
        ui::ListView *list = nullptr;
        std::vector<Row> rows;
    };
}

namespace screens {

    Screen *makeMenu(App &app, const std::string &title, const std::vector<std::string> &options, int checked,
                     std::function<void(int)> onChoice, const std::vector<std::string> &details) {
        return new MenuScreen(app, title, options, checked, std::move(onChoice), details);
    }

    Screen *makeKeyboard(App &app, const std::string &title, const std::string &initial, bool password,
                         std::function<void(const std::string &)> onDone) {
        return new KeyboardScreen(app, title, initial, password, std::move(onDone));
    }

    Screen *makeDialog(App &app, const std::string &title, const std::string &message,
                       const std::vector<std::string> &buttons, std::function<void(int)> onChoice, bool destructive) {
        return new DialogScreen(app, title, message, buttons, std::move(onChoice), destructive);
    }
}
