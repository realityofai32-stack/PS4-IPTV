#include "common.h"
#include "../core/utf8.h"
#include "../platform/clock.h"

using namespace c2d;

namespace screens {

    void header(C2DObject *parent, const std::string &title, const std::string &subtitle) {
        ui::label(parent, title, theme::TITLE, theme::SAFE_X, theme::SAFE_Y, ui::Weight::SemiBold);
        if (!subtitle.empty()) {
            auto *s = ui::label(parent, subtitle, theme::LABEL, theme::SAFE_X, theme::SAFE_Y + 62, ui::Weight::Regular,
                                theme::textDim());
            s->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X);
        }
    }

    ui::HintBar *hintBar(C2DObject *parent, const Hints &hints) {
        auto *bar = new ui::HintBar();
        bar->setHints(hints);
        parent->add(bar);
        return bar;
    }

    std::string hostOf(const std::string &server) {
        size_t s = server.find("://");
        return s == std::string::npos ? server : server.substr(s + 3);
    }

    std::string mask(const std::string &secret) {
        std::string out;
        size_t n = utf8::length(secret);
        for (size_t i = 0; i < n; i++) {
            out += "\xE2\x80\xA2";  // bullet
        }
        return out;
    }

    std::string accountSummary(const iptv::AccountInfo &a) {
        // the provider's status word is shown as sent; only the app's own words are translated
        std::string s = a.status.empty() ? i18n::tr("account.active") : a.status;
        if (a.trial) {
            s = i18n::tr("account.trial", {s});
        }
        s += "  \xE2\x80\xA2  " + (a.expiresAt > 0 ? i18n::tr("account.expires", {clockx::localDate(a.expiresAt)})
                                                   : i18n::tr("account.no_expiry"));
        if (a.maxConnections > 0) {
            s += "  \xE2\x80\xA2  " + i18n::tr("account.connections", {std::to_string(a.activeConnections),
                                                                        std::to_string(a.maxConnections)});
        }
        return s;
    }

    FieldRow::FieldRow(const FloatRect &rect, const std::string &caption) : RectangleShape(rect) {
        setCornersRadius(theme::RADIUS_SMALL);
        setCornerPointCount(8);
        captionLabel = ui::label(this, caption, theme::BODY, 28, ui::Label::centerOffset(theme::BODY, rect.height),
                                 ui::Weight::SemiBold, theme::textDim());
        valueLabel = ui::label(this, "", theme::BODY, 360, ui::Label::centerOffset(theme::BODY, rect.height));
        valueLabel->setMaxWidth(rect.width - 360 - 28);
        setFocused(false);
    }

    void FieldRow::setValue(const std::string &value, bool placeholder) {
        valueLabel->setText(value);
        valueLabel->setColor(placeholder ? theme::textMuted() : theme::text());
    }

    void FieldRow::setCaption(const std::string &caption) {
        captionLabel->setText(caption);
    }

    void FieldRow::setFocused(bool focused) {
        setFillColor(focused ? theme::surfaceFocus() : theme::surface());
        setOutlineColor(theme::accent());
        setOutlineThickness(focused ? theme::FOCUS_BORDER : 0);
        captionLabel->setColor(focused ? theme::text() : theme::textDim());
    }
}
