#include "common.h"
#include "../core/utf8.h"
#include "../iptv/m3u.h"
#include "../platform/clock.h"

using namespace c2d;

namespace screens {

    ui::Label *header(C2DObject *parent, const std::string &title, const std::string &subtitle) {
        // the V7 symbol in front of every screen title: one brand, one place; the mark sits on the title's
        // cap height (a 44 px semibold line: caps ~32 px tall, ~12 px below the label top)
        const float mark = layout::HEADER_MARK_H;
        auto *m = new ui::BrandMark(mark);
        m->setPosition(theme::SAFE_X, layout::HEADER_Y + std::round(ui::Label::centerOffset(theme::TITLE, 56) + 5));
        parent->add(m);
        const float x = theme::SAFE_X + m->getSize().x + theme::SPACE_M;
        auto *t = ui::label(parent, title, theme::TITLE, x, layout::HEADER_Y, ui::Weight::SemiBold);
        t->setMaxWidth(theme::SCREEN_W - theme::SAFE_X - x);
        if (!subtitle.empty()) {
            auto *s = ui::label(parent, subtitle, theme::LABEL, x, layout::HEADER_Y + 62, ui::Weight::Regular,
                                theme::textSecondary());
            s->setMaxWidth(theme::SCREEN_W - theme::SAFE_X - x);
        }
        return t;
    }

    namespace {
        // y of a label of `size` whose baseline matches the header title's (baseline ~0.99 x size below the top)
        float onTitleBaseline(unsigned size) {
            return layout::HEADER_Y + std::round(0.99f * (float) (theme::TITLE - size));
        }
    }

    HeaderContext::HeaderContext(C2DObject *parent, ui::Label *t) : title(t) {
        lead = ui::label(parent, "", theme::BODY, 0, onTitleBaseline(theme::BODY), ui::Weight::SemiBold,
                         theme::textPrimary());
        rest = ui::label(parent, "", theme::LABEL, 0, onTitleBaseline(theme::LABEL), ui::Weight::Regular,
                         theme::textSecondary());
    }

    void HeaderContext::set(const std::string &l, const std::string &r) {
        lead->setText(l);
        rest->setText(r);
        float x = title->getPosition().x + title->width() + layout::HEADER_CONTEXT_GAP;
        // leaves room for a status at the right end of the line
        const float limit = layout::SAFE_RIGHT - 340;
        lead->setPosition(std::round(x), onTitleBaseline(theme::BODY));
        lead->setMaxWidth(std::max(0.0f, limit - x));
        float rx = l.empty() ? x : x + lead->width() + theme::SPACE_M;
        rest->setPosition(std::round(rx), onTitleBaseline(theme::LABEL));
        rest->setMaxWidth(std::max(0.0f, limit - rx));
    }

    ui::Label *headerStatus(C2DObject *parent) {
        auto *l = ui::label(parent, "", theme::LABEL, 0, onTitleBaseline(theme::LABEL), ui::Weight::Regular,
                            theme::textMuted());
        l->setAlign(ui::Align::Right, layout::SAFE_RIGHT);
        l->setMaxWidth(320);
        return l;
    }

    void breadcrumb(C2DObject *parent, const std::string &section) {
        // the same lockup as a page header (symbol + name), smaller: details belong to their section
        auto *m = new ui::BrandMark(layout::detail::CRUMB_MARK_H);
        m->setPosition(theme::SAFE_X, layout::HEADER_Y + 3);
        parent->add(m);
        ui::label(parent, section, theme::LABEL, theme::SAFE_X + m->getSize().x + theme::SPACE_S, layout::HEADER_Y,
                  ui::Weight::SemiBold, theme::textSecondary());
    }

    void bottomFade(C2DObject *parent) {
        const float solidY = layout::BOTTOM_SOLID_Y;
        ui::fade(parent, FloatRect(0, layout::BOTTOM_FADE_Y, theme::SCREEN_W, solidY - layout::BOTTOM_FADE_Y),
                 theme::backgroundSecondary(), ui::Edge::Bottom);
        ui::box(parent, FloatRect(0, solidY, theme::SCREEN_W, theme::SCREEN_H - solidY), theme::backgroundSecondary(), 0);
    }

    void bindBackdrop(App &app, ui::Backdrop *backdrop, const std::string &url) {
        std::shared_ptr<ImageSet> big = app.images().get(ImageKind::PosterLarge, url);
        std::shared_ptr<ImageSet> small = big ? nullptr : app.images().get(ImageKind::Poster, url);
        const std::shared_ptr<ImageSet> &img = big ? big : small;
        backdrop->set(img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
    }

    void hudShade(C2DObject *parent, float height, float solid, bool top) {
        const Color c = theme::overlay();
        const float w = theme::SCREEN_W;
        if (top) {
            ui::box(parent, FloatRect(0, 0, w, solid), c, 0);
            ui::fade(parent, FloatRect(0, solid, w, height - solid), c, ui::Edge::Top);
        } else {
            ui::fade(parent, FloatRect(0, 0, w, height - solid), c, ui::Edge::Bottom);
            ui::box(parent, FloatRect(0, height - solid, w, solid), c, 0);
        }
    }

    ui::HintBar *hintBar(C2DObject *parent, const Hints &hints) {
        auto *bar = new ui::HintBar();
        bar->setHints(hints);
        parent->add(bar);
        return bar;
    }

    std::string aspectName(display::Aspect aspect) {
        return aspect == display::Aspect::Source ? tr("geometry.aspect_source") : display::aspectLabel(aspect);
    }

    std::string cropName(display::Crop crop) {
        return crop == display::Crop::None ? tr("geometry.crop_none")
                                           : crop == display::Crop::Fill ? tr("geometry.crop_fill")
                                                                         : display::cropLabel(crop);
    }

    std::string zoomName(int percent) {
        return tr("geometry.zoom_value", {std::to_string(display::clampZoom(percent))});
    }

    std::string positionName(int step, bool horizontal) {
        step = display::clampPosition(step);
        if (step == 0) {
            return tr("geometry.centered");
        }
        std::string amount = std::to_string(std::abs(step) * 100 / display::POSITION_STEPS);
        const char *key = horizontal ? (step < 0 ? "geometry.left" : "geometry.right")
                                     : (step < 0 ? "geometry.up" : "geometry.down");
        return tr(key, {amount});
    }

    std::string geometrySummary(const display::Geometry &g) {
        const std::string sep = "  \xC2\xB7  ";
        return aspectName(g.aspect) + sep + cropName(g.crop) + sep + zoomName(g.zoom);
    }

    std::string hostOf(const std::string &server) {
        size_t s = server.find("://");
        return s == std::string::npos ? server : server.substr(s + 3);
    }

    std::string sourceTypeName(iptv::SourceType type) {
        return tr(type == iptv::SourceType::M3u ? "source.type_m3u_short" : "source.type_xtream");
    }

    std::string sourceLocation(const iptv::Profile &p) {
        return p.isPlaylist() ? m3u::displayUrl(p.playlistUrl) : hostOf(p.server);
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
                                 ui::Weight::SemiBold, theme::textSecondary());
        valueLabel = ui::label(this, "", theme::BODY, 360, ui::Label::centerOffset(theme::BODY, rect.height));
        valueLabel->setMaxWidth(rect.width - 360 - 28);
        setFocused(false);
    }

    void FieldRow::setValue(const std::string &value, bool placeholder) {
        valueLabel->setText(value);
        valueLabel->setColor(placeholder ? theme::textMuted() : theme::textPrimary());
    }

    void FieldRow::setCaption(const std::string &caption) {
        captionLabel->setText(caption);
    }

    void FieldRow::setFocused(bool focused) {
        setFillColor(focused ? theme::cardHover() : theme::surface());
        setOutlineColor(theme::accent());
        setOutlineThickness(focused ? theme::FOCUS_BORDER : 0);
        captionLabel->setColor(focused ? theme::textPrimary() : theme::textSecondary());
    }
}
