// The choice picker (screens::makeChoicePicker): one compact modal list for every multi-value setting.
//
//   ┌─────────────────────────────────┐
//   │ Aspect ratio                    │
//   │ ✓ Auto / Source                 │   current value: check mark
//   │   16:9                          │   focused row: the one strong focus (ui::RowFrame focused)
//   │   4:3                     ▮     │   scroll bar when there are more rows than fit
//   │ [X] Select   [O] Cancel         │   hints for the device in use
//   └─────────────────────────────────┘
// Logic: ui::ChoiceModel (host-tested). The screen below stays visible (modal: darkened by the scrim; the video player
// keeps ticking and drawing under it, App::logic / run).

#include "common.h"
#include "../ui/choice_model.h"

using namespace c2d;

namespace {

    const float CARD_W = 640;
    const float ROW_H = 72;
    const float PAD = 32;
    const float TITLE_H = 64;
    const int MAX_ROWS = 7;

    class ChoicePickerScreen : public Screen, public ui::ListView::Adapter {
    public:
        ChoicePickerScreen(App &a, const std::string &title, std::vector<std::string> opts, int current,
                           std::function<void(int)> choice)
                : Screen(a, true), options(std::move(opts)), model((int) options.size(), current),
                  onChoice(std::move(choice)) {
            const int visible = std::max(1, std::min(MAX_ROWS, (int) options.size()));
            const float listH = (float) visible * ROW_H;
            const float h = PAD + TITLE_H + listH + 20 + layout::HINTS_H + PAD - 8;
            const float x = std::round((theme::SCREEN_W - CARD_W) / 2);
            const float y = std::round((theme::SCREEN_H - h) / 2);
            card = ui::box(this, FloatRect(x, y, CARD_W, h), theme::panel(), theme::RADIUS_LARGE);
            card->setOutlineColor(theme::divider());
            card->setOutlineThickness(1);
            auto *t = ui::label(card, title, theme::HEADING, PAD, PAD, ui::Weight::SemiBold);
            t->setMaxWidth(CARD_W - 2 * PAD);
            const float listW = CARD_W - 2 * PAD + 8;
            list = new ui::ListView(FloatRect(PAD - 8, PAD + TITLE_H, listW, listH), ROW_H, 0, this);
            card->add(list);
            auto *hints = new ui::HintBar();
            hints->setOwner(this);
            hints->setSize(CARD_W - 2 * PAD, layout::HINTS_H);
            hints->setPosition(PAD, PAD + TITLE_H + listH + 20);
            hints->setHints({{A::Confirm, tr("common.select")}, {A::Back, tr("common.cancel")}});
            card->add(hints);
            list->setFocused(true);
            list->setSelected(model.selected());
        }

        const char *name() const override { return "choice-picker"; }

        int count() override { return (int) options.size(); }

        C2DObject *createRow(float w, float h) override {
            Row r;
            r.bg = new ui::RowFrame(w, h, -1);
            r.check = ui::label(r.bg, "", theme::BODY, 20, ui::Label::centerOffset(theme::BODY, h), ui::Weight::SemiBold,
                                theme::accentText());
            r.name = ui::label(r.bg, "", theme::BODY, 60, ui::Label::centerOffset(theme::BODY, h));
            r.name->setMaxWidth(w - 60 - 24);
            rows.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int i, bool, bool focused) override {
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                const bool current = model.isCurrent(i);
                r.check->setText(current ? "\xE2\x9C\x93" : "");
                r.name->setText(options[(size_t) i]);
                r.name->setColor(focused || current ? theme::textStrong() : theme::textPrimary());
                r.name->setWeight(focused || current ? ui::Weight::SemiBold : ui::Weight::Regular);
                r.bg->setState(focused);   // only the focused row gets the focus treatment
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (model.apply(e)) {
                case ui::ChoiceModel::Result::Moved:
                    list->setSelected(model.selected());
                    redraw();
                    return;
                case ui::ChoiceModel::Result::Confirmed:
                case ui::ChoiceModel::Result::Cancelled: {
                    int chosen = model.chosen();
                    auto cb = onChoice;
                    app.pop();   // first: the callback may rebuild the screens (language, accent) or open another
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
            ui::RowFrame *bg;
            ui::Label *check;
            ui::Label *name;
        };

        std::vector<std::string> options;
        ui::ChoiceModel model;
        std::function<void(int)> onChoice;
        RectangleShape *card = nullptr;
        ui::ListView *list = nullptr;
        std::vector<Row> rows;
    };
}

namespace screens {
    Screen *makeChoicePicker(App &app, const std::string &title, const std::vector<std::string> &options, int current,
                             std::function<void(int)> onChoice) {
        return new ChoicePickerScreen(app, title, options, current, std::move(onChoice));
    }
}
