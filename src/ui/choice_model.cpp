#include <algorithm>

#include "choice_model.h"

namespace ui {

    ChoiceModel::ChoiceModel(int count, int current) : n(std::max(0, count)) {
        cur = current >= 0 && current < n ? current : -1;
        sel = cur >= 0 ? cur : 0;
    }

    ChoiceModel::Result ChoiceModel::apply(const InputEvent &e) {
        if (done() || n == 0) {
            if (n == 0 && !e.repeat && (e.button == PadButton::Circle || e.button == PadButton::Cross)) {
                result = Result::Cancelled;
                return result;
            }
            return Result::None;
        }
        int to = sel;
        switch (e.button) {
            case PadButton::Up:
                to = sel - 1;
                break;
            case PadButton::Down:
                to = sel + 1;
                break;
            case PadButton::L2:
                to = sel - PAGE;
                break;
            case PadButton::R2:
                to = sel + PAGE;
                break;
            case PadButton::Cross:
                if (e.repeat) {
                    return Result::None;
                }
                result = Result::Confirmed;
                return result;
            case PadButton::Circle:
            case PadButton::Options:
                if (e.repeat) {
                    return Result::None;
                }
                result = Result::Cancelled;
                return result;
            default:
                return Result::None;
        }
        to = std::min(std::max(to, 0), n - 1);   // stops at the ends (no wrap: a held key never loops around)
        if (to == sel) {
            return Result::None;
        }
        sel = to;
        return Result::Moved;
    }
}
