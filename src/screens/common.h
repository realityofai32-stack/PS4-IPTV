// Shared screen building blocks.

#ifndef PS4IPTV_SCREENS_COMMON_H
#define PS4IPTV_SCREENS_COMMON_H

#include <string>
#include <utility>
#include <vector>

#include "../app/app.h"
#include "../app/screens.h"
#include "../i18n/i18n.h"
#include "../ui/text.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

// every screen text comes from the localization tables (src/i18n): tr("key"), tr("key", {args})
using i18n::tr;

namespace screens {

    using Hints = std::vector<std::pair<ui::Glyph, std::string>>;

    // page title (+ optional subtitle) at the top-left of a full screen
    void header(c2d::C2DObject *parent, const std::string &title, const std::string &subtitle = "");

    ui::HintBar *hintBar(c2d::C2DObject *parent, const Hints &hints);

    std::string hostOf(const std::string &server);

    std::string mask(const std::string &secret);

    // "Active - expires 21 Sep 2027 - 0/1 connections"
    std::string accountSummary(const iptv::AccountInfo &account);

    // a row with a caption on the left and a value on the right, used by forms and settings
    class FieldRow : public c2d::RectangleShape {
    public:
        FieldRow(const c2d::FloatRect &rect, const std::string &caption);

        void setValue(const std::string &value, bool placeholder = false);

        void setCaption(const std::string &caption);

        void setFocused(bool focused);

    private:
        ui::Label *captionLabel;
        ui::Label *valueLabel;
    };
}

#endif // PS4IPTV_SCREENS_COMMON_H
