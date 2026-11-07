#include <algorithm>
#include <cmath>

#include "components.h"
#include "focus_colors.h"
#include "icon_shapes.h"

using namespace c2d;

namespace ui {

    namespace {
        const float PI = 3.14159265f;

        ConvexShape *polygon(C2DObject *parent, std::initializer_list<Vector2f> points) {
            auto *p = new ConvexShape(points.size());
            size_t i = 0;
            for (const Vector2f &v: points) {
                p->setPoint(i++, v);
            }
            parent->add(p);
            return p;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    // V7 symbol on a 100 x 80 grid (x right, y down):
    //   left arm   (0,0) (22,0) (58,80) (36,80)       the V's left stroke
    //   right arm  (36,80) (58,80) (100,0) (78,0)     the V's right stroke = the 7's stem
    //   bar        (46,0) (100,0) (91.5,16) (46,16)   the 7's top bar (its end follows the stem's slope)

    BrandMark::BrandMark(float h) : RectangleShape(FloatRect(0, 0, std::round(h * ASPECT), h)) {
        setFillColor(theme::none());
        const float u = h / 80.0f;
        auto p = [u](float x, float y) { return Vector2f(x * u, y * u); };
        left = polygon(this, {p(0, 0), p(22, 0), p(58, 80), p(36, 80)});
        right = polygon(this, {p(36, 80), p(58, 80), p(100, 0), p(78, 0)});
        bar = polygon(this, {p(46, 0), p(100, 0), p(91.5f, 16), p(46, 16)});
        setColors(theme::accentText(), theme::accent());
    }

    void BrandMark::setColors(const Color &arm, const Color &stem) {
        left->setFillColor(arm);
        right->setFillColor(stem);
        bar->setFillColor(stem);
    }

    Brand::Brand(const std::string &name, float markHeight, unsigned textSize)
            : RectangleShape(FloatRect(0, 0, 1, markHeight)) {
        setFillColor(theme::none());
        auto *mark = new BrandMark(markHeight);
        add(mark);
        float x = mark->getSize().x + std::round(markHeight * 0.32f);
        auto *l = new Label(name, textSize, Weight::SemiBold, theme::textStrong());
        l->setPosition(x, Label::centerOffset(textSize, markHeight));
        add(l);
        setSize(x + l->width(), markHeight);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    Chip::Chip(const std::string &text, float x, float y, float w, float h)
            : RectangleShape(FloatRect(x, y, std::max(w, h), h)), fixedWidth(w) {
        setCornersRadius(h / 2);
        setCornerPointCount(10);
        caption = new Label(text, theme::LABEL, Weight::SemiBold, theme::textSecondary());
        add(caption);
        layout();
        setState(false, false);
    }

    void Chip::layout() {
        const float pad = 28;
        float h = getSize().y;
        caption->setMaxWidth(0);
        float w = fixedWidth > 0 ? fixedWidth : std::round(caption->width() + 2 * pad);
        setSize(w, h);
        setCornersRadius(h / 2);
        caption->setMaxWidth(w - 24);
        caption->setAlign(Align::Center, w);
        caption->setPosition(0, Label::centerOffset(theme::LABEL, h));
    }

    void Chip::setText(const std::string &text) {
        if (text != caption->getText()) {
            caption->setText(text);
            layout();
        }
    }

    void Chip::setState(bool sel, bool foc) {
        selected = sel;
        focused = foc;
        // focused: the accent fill; selected: the accent's dark tint; rest: a quiet elevated chip (ui/focus_style.h)
        focus::Look l = focus::look(focus::Role::Chip, focused, selected);
        setFillColor(focus::color(l.fill));
        setOutlineThickness(0);
        caption->setColor(focus::color(l.text));
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    ProgressBar::ProgressBar(const FloatRect &rect, bool onImage) : RectangleShape(rect) {
        float r = rect.height / 2;
        setCornersRadius(r);
        setCornerPointCount(6);
        setFillColor(onImage ? theme::progressTrackOnImage() : theme::progressTrack());
        fill = new RectangleShape(FloatRect(0, 0, rect.height, rect.height));
        fill->setCornersRadius(r);
        fill->setCornerPointCount(6);
        fill->setFillColor(theme::accent());
        add(fill);
        setFraction(0);
    }

    void ProgressBar::setFraction(float f) {
        float w = getSize().x;
        float h = getSize().y;
        fill->setVisibility(f > 0.0005f ? Visibility::Visible : Visibility::Hidden);
        fill->setSize(std::max(h, std::round(w * std::min(1.0f, std::max(0.0f, f)))), h);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    IconButton::IconButton(Icon icon, float s) : RectangleShape(FloatRect(0, 0, s, s)) {
        setCornersRadius(s / 2);
        setCornerPointCount(16);
        const float c = s / 2;
        const float t = std::max(2.0f, std::round(s * 0.05f));
        switch (icon) {
            case Icon::Search: {
                float r = s * 0.16f;
                auto *ring = new CircleShape(r);
                ring->setPointCount(28);
                ring->setOrigin(Origin::Center);
                ring->setPosition(c - s * 0.05f, c - s * 0.05f);
                addPart(ring, true);
                auto *handle = new RectangleShape(FloatRect(0, 0, s * 0.16f, t * 1.3f));
                handle->setOrigin(Origin::Left);
                float d = (r + t * 0.4f) * 0.7071f;
                handle->setPosition(c - s * 0.05f + d, c - s * 0.05f + d);
                handle->setRotation(45);
                addPart(handle, false);
                break;
            }
            case Icon::Favorites: {
                // five-pointed star (star-shaped around its centre: the shape's fan triangulation draws it)
                auto *star = new ConvexShape(10);
                float R = s * 0.24f;
                for (int i = 0; i < 10; i++) {
                    float r = i % 2 == 0 ? R : R * 0.45f;
                    float a = -PI / 2 + (float) i * PI / 5;
                    star->setPoint((size_t) i, Vector2f(c + r * std::cos(a), c + s * 0.02f + r * std::sin(a)));
                }
                addPart(star, true);
                break;
            }
            case Icon::Downloads: {
                auto *shaft = new RectangleShape(FloatRect(c - t * 0.65f, c - s * 0.22f, t * 1.3f, s * 0.24f));
                addPart(shaft, false);
                float hw = s * 0.13f;
                auto *head = new ConvexShape(3);
                head->setPoint(0, Vector2f(c - hw, c - s * 0.02f));
                head->setPoint(1, Vector2f(c + hw, c - s * 0.02f));
                head->setPoint(2, Vector2f(c, c + s * 0.13f));
                addPart(head, false);
                auto *base = new RectangleShape(FloatRect(c - s * 0.19f, c + s * 0.19f, s * 0.38f, t * 1.3f));
                addPart(base, false);
                break;
            }
            case Icon::Settings: {
                // a gear (ui/icon_shapes.h): one outlined silhouette with eight teeth + an outlined hub - the same
                // stroke, size and weight as the search ring and the favorites star
                std::vector<icons::Point> pts = icons::gear(c, c, s);
                auto *gear = new ConvexShape(pts.size());
                for (size_t i = 0; i < pts.size(); i++) {
                    gear->setPoint(i, Vector2f(pts[i].x, pts[i].y));
                }
                addPart(gear, true);
                auto *hub = new CircleShape(s * icons::GEAR_HUB);
                hub->setPointCount(20);
                hub->setOrigin(Origin::Center);
                hub->setPosition(c, c);
                addPart(hub, true);
                break;
            }
        }
        for (auto &p: parts) {
            if (p.second && p.first->getOutlineThickness() <= 0) {
                p.first->setOutlineThickness(t);
            }
        }
        badge = new CircleShape(s * 0.17f);
        badge->setPointCount(20);
        badge->setPosition(s * 0.66f, -s * 0.06f);
        badge->setOutlineColor(theme::background());
        badge->setOutlineThickness(3);
        add(badge);
        badgeText = new Label("", (unsigned) std::round(s * 0.23f), Weight::SemiBold, theme::onAccent());
        badgeText->setAlign(Align::Center, s * 0.34f);
        badgeText->setPosition(0, Label::centerOffset((unsigned) std::round(s * 0.23f), s * 0.34f));
        badge->add(badgeText);
        setBadge(0);
        setFocused(false);
    }

    void IconButton::addPart(Shape *shape, bool outline) {
        add(shape);
        parts.emplace_back(shape, outline);
    }

    void IconButton::setFocused(bool f) {
        focused = f;
        // at rest only the icon is drawn (no disc competing with the content); focused: an accent disc
        focus::Look l = focus::look(focus::Role::Icon, focused);
        setFillColor(focus::color(l.fill));
        Color fg = focus::color(l.text);
        for (auto &p: parts) {
            if (p.second) {
                p.first->setFillColor(theme::none());
                p.first->setOutlineColor(fg);
            } else {
                p.first->setFillColor(fg);
            }
        }
        badge->setFillColor(theme::accent());
    }

    void IconButton::setBadge(int count) {
        badge->setVisibility(count > 0 ? Visibility::Visible : Visibility::Hidden);
        badgeText->setText(count > 9 ? "9+" : std::to_string(count));
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    // one primary section of the navigation: text at rest, an accent underline when it is the current section,
    // an accent pill while focused
    class NavTab : public RectangleShape {
    public:
        NavTab(const std::string &text, float x, float h) : RectangleShape(FloatRect(x, 0, 1, h)) {
            setCornersRadius(h / 2);
            setCornerPointCount(10);
            caption = new Label(text, theme::BODY, Weight::Regular, theme::textSecondary());
            caption->setWeight(Weight::SemiBold);   // measured at the widest weight: no width jump on focus
            float w = std::round(caption->width() + 2 * layout::home::TAB_PAD);
            setSize(w, h);
            caption->setAlign(Align::Center, w);
            caption->setPosition(0, Label::centerOffset(theme::BODY, h));
            add(caption);
            underline = new RectangleShape(FloatRect(layout::home::TAB_PAD, h - 7, w - 2 * layout::home::TAB_PAD, 3));
            underline->setCornersRadius(1.5f);
            underline->setCornerPointCount(4);
            add(underline);
            setState(false, false);
        }

        void setState(bool current, bool focused) {
            focus::Look l = focus::look(focus::Role::NavTab, focused, current);
            setFillColor(focus::color(l.fill));
            caption->setWeight(current || focused ? Weight::SemiBold : Weight::Regular);
            caption->setColor(focus::color(l.text));
            underline->setFillColor(theme::accent());
            underline->setVisibility(current && !focused ? Visibility::Visible : Visibility::Hidden);
        }

        float width() const { return getSize().x; }

    private:
        Label *caption;
        RectangleShape *underline;
    };

    TopNav::TopNav(const std::vector<std::string> &sections, const std::vector<Icon> &icons)
            : RectangleShape(FloatRect(theme::SAFE_X, layout::home::NAV_Y, theme::CONTENT_W, layout::home::NAV_H)) {
        using namespace layout::home;
        setFillColor(theme::none());
        const float h = getSize().y;
        // the V7 symbol alone: the product name is not repeated in the navigation
        auto *mark = new BrandMark(BRAND_MARK_H);
        mark->setPosition(0, std::round((h - BRAND_MARK_H) / 2));
        add(mark);
        float x = mark->getSize().x + TABS_GAP;
        for (const std::string &s: sections) {
            auto *tab = new NavTab(s, x, TAB_H);
            tab->setPosition(x, std::round((h - TAB_H) / 2));
            add(tab);
            pills.push_back(tab);
            x += tab->width() + TAB_GAP;
        }
        clock = new Label("", theme::BODY, Weight::SemiBold, theme::textPrimary());
        clock->setAlign(Align::Right, getSize().x);
        clock->setPosition(0, Label::centerOffset(theme::BODY, h));
        add(clock);
        // small round actions right-aligned before the clock
        float right = getSize().x - CLOCK_W;
        for (size_t i = 0; i < icons.size(); i++) {
            auto *b = new IconButton(icons[i], ICON);
            float bx = right - (float) (icons.size() - i) * (ICON + ICON_GAP) + ICON_GAP;
            b->setPosition(bx, std::round((h - ICON) / 2));
            add(b);
            buttons.push_back(b);
        }
        iconCaption = new Label("", theme::CAPTION, Weight::SemiBold, theme::textSecondary());
        iconCaption->setVisibility(Visibility::Hidden);
        add(iconCaption);
        setFocus(-1);
    }

    void TopNav::setFocus(int index) {
        focus = index;
        for (int i = 0; i < (int) pills.size(); i++) {
            pills[(size_t) i]->setState(i == current, i == focus);
        }
        for (int i = 0; i < (int) buttons.size(); i++) {
            buttons[(size_t) i]->setFocused(i + (int) pills.size() == focus);
        }
        iconCaption->setVisibility(focus >= (int) pills.size() ? Visibility::Visible : Visibility::Hidden);
    }

    void TopNav::setCurrent(int section) {
        current = section;
        setFocus(focus);
    }

    void TopNav::setClock(const std::string &text) {
        clock->setText(text);
    }

    const std::string &TopNav::clockText() const {
        return clock->getText();
    }

    void TopNav::setBadge(int icon, int count) {
        if (icon >= 0 && icon < (int) buttons.size()) {
            buttons[(size_t) icon]->setBadge(count);
        }
    }

    void TopNav::setIconCaption(const std::string &text) {
        int i = focus - (int) pills.size();
        if (i < 0 || i >= (int) buttons.size()) {
            return;
        }
        iconCaption->setText(text);
        const Vector2f &p = buttons[(size_t) i]->getPosition();
        float w = iconCaption->width();
        // centred under the icon, kept inside the bar's right edge (the TV-safe area)
        float x = std::min(p.x + layout::home::ICON / 2 - w / 2, getSize().x - w);
        iconCaption->setPosition(std::round(x), getSize().y + 4);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    RectangleShape *fade(C2DObject *parent, const FloatRect &rect, const Color &color, Edge solid) {
        auto *box = new RectangleShape(rect);
        box->setFillColor(theme::none());
        for (int i = 0; i < 4; i++) {
            box->add(new GradientRectangle(FloatRect(0, 0, 1, 1)));
        }
        refade(box, rect, color, solid);
        if (parent) {
            parent->add(box);
        }
        return box;
    }

    void refade(RectangleShape *box, const FloatRect &rect, const Color &color, Edge solid) {
        box->setPosition(rect.left, rect.top);
        box->setSize(rect.width, rect.height);
        // smoothstep at 0, 1/4, 1/2, 3/4, 1 (u = 0 at the transparent edge, 1 at the solid edge)
        const float steps[5] = {0.0f, 0.15625f, 0.5f, 0.84375f, 1.0f};
        const bool vertical = solid == Edge::Top || solid == Edge::Bottom;
        const float len = vertical ? rect.height : rect.width;
        int i = 0;
        for (C2DObject *child: box->getChilds()) {
            auto *g = dynamic_cast<GradientRectangle *>(child);
            if (!g || i >= 4) {
                continue;
            }
            // segment i runs from the transparent side towards the solid side
            float a0 = steps[i];
            float a1 = steps[i + 1];
            float from = len * (float) i / 4;
            float to = len * (float) (i + 1) / 4;
            bool reversed = solid == Edge::Top || solid == Edge::Left;   // solid side at the origin
            float start = reversed ? len - to : from;
            float size = to - from;
            FloatRect r = vertical ? FloatRect(0, start, rect.width, size) : FloatRect(start, 0, size, rect.height);
            g->setPosition(r.left, r.top);
            g->setSize(r.width, r.height);
            Color c0 = theme::withAlpha(color, (uint8_t) std::lround((float) color.a * (reversed ? a1 : a0)));
            Color c1 = theme::withAlpha(color, (uint8_t) std::lround((float) color.a * (reversed ? a0 : a1)));
            g->setColor(c0, c1, vertical ? GradientRectangle::Down : GradientRectangle::Right);
            i++;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    RowFrame::RowFrame(float w, float h, float dividerInset) : RectangleShape(FloatRect(0, 0, w, h)) {
        setCornersRadius(theme::RADIUS);
        setCornerPointCount(8);
        if (dividerInset >= 0) {
            line = new RectangleShape(FloatRect(dividerInset, h - 1, w - dividerInset, 1));
            line->setFillColor(theme::dividerSoft());
            add(line);
        }
        const float inset = std::round(h * 0.22f);
        edge = new RectangleShape(FloatRect(0, inset, layout::ROW_EDGE_W, h - 2 * inset));
        edge->setCornersRadius(layout::ROW_EDGE_W / 2);
        edge->setCornerPointCount(4);
        add(edge);
        setState(false, false);
    }

    void RowFrame::setState(bool focused, bool selected) {
        // focused: the tinted surface and the accent edge; selected (the open item of an unfocused list): a quiet edge
        focus::Look l = focus::look(focus::Role::Row, focused, selected);
        setFillColor(focus::color(l.fill));
        setOutlineThickness(0);
        edge->setFillColor(focus::color(l.edge));
        edge->setVisibility(l.edge != focus::Edge::None ? Visibility::Visible : Visibility::Hidden);
        if (line) {
            line->setVisibility(focused || !divider ? Visibility::Hidden : Visibility::Visible);
        }
    }

    void RowFrame::setDivider(bool shown) {
        divider = shown;
        if (line) {
            line->setVisibility(divider && getFillColor().a == 0 ? Visibility::Visible : Visibility::Hidden);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    EmptyState::EmptyState(const FloatRect &rect, bool leftAligned) : RectangleShape(rect), left(leftAligned) {
        setFillColor(theme::none());
        const float pad = left ? 0 : 40;
        title = new Label("", theme::HEADING, Weight::SemiBold, theme::textPrimary());
        title->setMaxWidth(rect.width - pad);
        add(title);
        message = new Label("", theme::BODY, Weight::Regular, theme::textSecondary());
        message->setMaxWidth(std::min(rect.width - pad, 1100.0f));
        message->setMaxLines(4);
        add(message);
        action = new Label("", theme::LABEL, Weight::SemiBold, theme::accentText());
        action->setMaxWidth(rect.width - pad);
        add(action);
        if (!left) {
            title->setAlign(Align::Center, rect.width);
            message->setAlign(Align::Center, rect.width);
            action->setAlign(Align::Center, rect.width);
        }
        setVisibility(Visibility::Hidden);
    }

    void EmptyState::set(const std::string &t, const std::string &m, const std::string &a) {
        if (t.empty()) {
            setVisibility(Visibility::Hidden);
            return;
        }
        title->setText(t);
        message->setText(m);
        action->setText(a);
        float h = title->height() + (m.empty() ? 0 : 16 + message->height()) + (a.empty() ? 0 : 24 + action->height());
        float y = left ? 0 : std::round(std::max(0.0f, (getSize().y - h) / 2));
        title->setPosition(0, y);
        y += title->height() + 16;
        message->setPosition(0, y);
        y += m.empty() ? 0 : message->height() + 24;
        action->setPosition(0, y);
        setVisibility(Visibility::Visible);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    Backdrop::Backdrop(const FloatRect &rect, float fadeLeft, float fadeBottom, float fadeTop) : RectangleShape(rect) {
        setFillColor(theme::none());
        // no artwork: a faint neutral glow from the right edge (never the accent: large surfaces stay neutral)
        glow = new GradientRectangle(FloatRect(0, 0, rect.width, rect.height));
        glow->setColor(theme::withAlpha(theme::surface(), 120), theme::none(), GradientRectangle::Left);
        add(glow);
        // the colour field (Poster art with PosterStyle::washLength): solid behind the poster, fading out to its left
        washField = new RectangleShape(FloatRect(0, 0, 1, 1));
        washField->setFillColor(theme::background());
        washField->setVisibility(Visibility::Hidden);
        add(washField);
        washFade = fade(this, FloatRect(0, 0, 1, rect.height), theme::background(), Edge::Right);
        washFade->setVisibility(Visibility::Hidden);
        ambientRight = fade(this, FloatRect(0, 0, 1, 1), theme::background(), Edge::Left);
        ambientRight->setVisibility(Visibility::Hidden);
        ambientTop = fade(this, FloatRect(0, 0, 1, 1), theme::background(), Edge::Top);
        ambientTop->setVisibility(Visibility::Hidden);
        ambientBottom = fade(this, FloatRect(0, 0, 1, 1), theme::background(), Edge::Bottom);
        ambientBottom->setVisibility(Visibility::Hidden);
        image = new RectangleShape(FloatRect(0, 0, rect.width, rect.height));
        image->setFillColor(theme::backdropTint());   // texture modulation: the artwork itself is dimmed
        image->setVisibility(Visibility::Hidden);
        add(image);
        shade = new RectangleShape(FloatRect(0, 0, rect.width, rect.height));
        shade->setFillColor(theme::backdropShade());
        shade->setVisibility(Visibility::Hidden);
        add(shade);
        // a poster's left edge melts into the page (or its colour field): no rectangle, no split
        posterFade = fade(this, FloatRect(0, 0, std::round(rect.height * 0.42f), rect.height), theme::background(),
                          Edge::Left);
        posterFade->setVisibility(Visibility::Hidden);
        posterBottom = fade(this, FloatRect(0, 0, 1, 1), theme::background(), Edge::Bottom);
        posterBottom->setVisibility(Visibility::Hidden);
        // every edge that is not a screen edge dissolves into the page: no frame, no band
        if (fadeLeft > 0) {
            fade(this, FloatRect(0, 0, fadeLeft, rect.height), theme::background(), Edge::Left);
        }
        if (fadeBottom > 0) {
            fade(this, FloatRect(0, rect.height - fadeBottom, rect.width, fadeBottom), theme::background(), Edge::Bottom);
        }
        if (fadeTop > 0) {
            fade(this, FloatRect(0, 0, rect.width, fadeTop), theme::withAlpha(theme::background(), 200), Edge::Top);
        }
    }

    void Backdrop::setLevel(uint8_t tint, uint8_t shadeAlpha) {
        image->setFillColor(Color(tint, tint, (uint8_t) std::min(255, tint + 2), 255));
        shade->setFillColor(theme::withAlpha(theme::backdropShade(), shadeAlpha));
    }

    void Backdrop::setAmbient(const Color &color, const FloatRect &core, float fadeLeft, float fadeRight, float fadeY) {
        hide();
        glow->setVisibility(Visibility::Hidden);
        const float coreRight = std::min(getSize().x, core.left + core.width);
        const float right = std::min(getSize().x, coreRight + fadeRight);
        const float x0 = std::max(0.0f, core.left - fadeLeft);
        const float y0 = core.top - fadeY;
        const float h = core.height + 2 * fadeY;
        // the solid tint behind the poster, over its height plus both vertical fades
        washField->setFillColor(color);
        washField->setPosition(core.left, y0);
        washField->setSize(std::max(1.0f, coreRight - core.left), h);
        washField->setVisibility(Visibility::Visible);
        // eased out to the left and to the right (in its own colour: alpha to 0) ...
        if (core.left - x0 >= 1) {
            refade(washFade, FloatRect(x0, y0, core.left - x0, h), color, Edge::Right);
            washFade->setVisibility(Visibility::Visible);
        }
        if (right - coreRight >= 1) {
            refade(ambientRight, FloatRect(coreRight, y0, right - coreRight, h), color, Edge::Left);
            ambientRight->setVisibility(Visibility::Visible);
        }
        // ... and above / below: the page colour eased over it, so the corners round off as well
        if (fadeY >= 1) {
            refade(ambientTop, FloatRect(x0, y0, right - x0, fadeY), theme::background(), Edge::Top);
            refade(ambientBottom, FloatRect(x0, core.top + core.height, right - x0, fadeY), theme::background(),
                   Edge::Bottom);
            ambientTop->setVisibility(Visibility::Visible);
            ambientBottom->setVisibility(Visibility::Visible);
        }
        washed = false;   // the next set() lays its own field out again
    }

    void Backdrop::setAnchor(float a) {
        anchor = std::min(1.0f, std::max(0.0f, a));
        recrop = true;   // the next set() crops again (the texture stays referenced meanwhile)
    }

    void Backdrop::setPosterStyle(const PosterStyle &s) {
        style = s;
        recrop = true;
    }

    void Backdrop::hide() {
        image->setVisibility(Visibility::Hidden);
        shade->setVisibility(Visibility::Hidden);
        posterFade->setVisibility(Visibility::Hidden);
        posterBottom->setVisibility(Visibility::Hidden);
        washField->setVisibility(Visibility::Hidden);
        washFade->setVisibility(Visibility::Hidden);
        ambientRight->setVisibility(Visibility::Hidden);
        ambientTop->setVisibility(Visibility::Hidden);
        ambientBottom->setVisibility(Visibility::Hidden);
        glow->setVisibility(Visibility::Visible);
        info = Shown();
    }

    FloatRect Backdrop::artRect() const {
        if (!image->isVisible()) {
            return {};
        }
        return {image->getPosition().x, image->getPosition().y, image->getSize().x, image->getSize().y};
    }

    void Backdrop::set(const std::shared_ptr<Texture> &texture, const Vector2i &size, Art art, const fit::Rgb *average) {
        if (texture == nullptr || size.x <= 0 || size.y <= 0) {
            hide();   // `shown` stays referenced by the hidden shape until something else is shown
            return;
        }
        ambientRight->setVisibility(Visibility::Hidden);    // an ambient tint (setAmbient) ends with real artwork
        ambientTop->setVisibility(Visibility::Hidden);
        ambientBottom->setVisibility(Visibility::Hidden);
        const bool wantWash = art == Art::Poster && style.washLength > 0 && average != nullptr;
        if (texture == shown && art == info.art && !recrop && image->isVisible() && wantWash == washed) {
            return;
        }
        const float bw = getSize().x;
        const float bh = getSize().y;
        fit::Placement p;
        Shown s;
        s.art = art;
        if (art == Art::Landscape) {
            fit::Options o;
            o.anchorY = anchor;
            p = fit::place(fit::Mode::Cover, (float) size.x, (float) size.y, bw, bh, o);
            if (p.valid) {
                s.scale = bh / p.src.h;
                s.drawn = {-p.src.x * s.scale, -p.src.y * s.scale, (float) size.x * s.scale, (float) size.y * s.scale};
            }
        } else {
            fit::PosterArt spec;
            spec.height = bh * style.heightScale;
            spec.maxUpscale = style.maxUpscale;
            spec.maxWidth = style.maxWidth;
            spec.minHeight = style.minHeight;
            spec.rightBleed = style.rightBleed;
            spec.anchorY = style.anchorY >= 0 ? style.anchorY : anchor;
            fit::PosterArtPlacement pa = fit::posterArt((float) size.x, (float) size.y, bw, bh, spec);
            p = pa.placement;
            s.drawn = pa.drawn;
            s.scale = pa.scale;
            s.limited = pa.limited;
        }
        if (!p.valid) {
            hide();
            return;
        }
        image->setTexture(texture.get(), true);
        shown = texture;   // the previous texture is released only now
        recrop = false;
        const fit::Rect d = fit::pixelSnap(p.dst);
        s.visible = true;
        s.inBox = d;
        info = s;
        image->setTextureRect(texelRect(p.src));
        image->setSize(d.w, d.h);
        image->setPosition(d.x, d.y);
        shade->setSize(d.w, d.h);
        shade->setPosition(d.x, d.y);
        image->setVisibility(Visibility::Visible);
        shade->setVisibility(Visibility::Visible);
        glow->setVisibility(Visibility::Hidden);

        washed = wantWash;
        if (washed) {
            // the poster's own average colour as drawn, dimmed: the field it dissolves into (ui/image_fit.h)
            const Color tint = theme::backdropTint();
            const Color sh = theme::backdropShade();
            const Color page = theme::background();
            fit::Rgb w = fit::ambientWash(*average, {(float) tint.r, (float) tint.g, (float) tint.b},
                                          {(float) sh.r, (float) sh.g, (float) sh.b}, (float) sh.a / 255,
                                          {(float) page.r, (float) page.g, (float) page.b},
                                          WASH_LEVEL, WASH_DESATURATE, WASH_MAX);
            washColor = Color((uint8_t) std::lround(w.r), (uint8_t) std::lround(w.g), (uint8_t) std::lround(w.b));
        }
        const Color edgeColor = washed ? washColor : theme::background();
        if (art == Art::Poster) {
            // left fade: v3 width (0.42 x the box height, full box height) unless the style sizes it to the poster
            float fadeW = style.leftFade > 0 ? std::round(d.w * style.leftFade) : std::round(bh * 0.42f);
            refade(posterFade, FloatRect(d.x, 0, fadeW, bh), edgeColor, Edge::Left);
            posterFade->setVisibility(Visibility::Visible);
            float bottomH = style.bottomFade > 0 ? std::round(d.h * style.bottomFade) : 0;
            if (bottomH > 0) {
                refade(posterBottom, FloatRect(d.x, d.y + d.h - bottomH, bw - d.x, bottomH), edgeColor, Edge::Bottom);
            }
            posterBottom->setVisibility(bottomH > 0 ? Visibility::Visible : Visibility::Hidden);
        } else {
            posterFade->setVisibility(Visibility::Hidden);
            posterBottom->setVisibility(Visibility::Hidden);
        }
        if (washed) {
            washField->setFillColor(washColor);
            washField->setPosition(d.x, 0);
            washField->setSize(bw - d.x, bh);
            float x0 = std::max(style.washMinX, d.x - style.washLength);
            refade(washFade, FloatRect(x0, 0, std::max(1.0f, d.x - x0), bh), washColor, Edge::Right);
        }
        washField->setVisibility(washed ? Visibility::Visible : Visibility::Hidden);
        washFade->setVisibility(washed ? Visibility::Visible : Visibility::Hidden);
    }
}
