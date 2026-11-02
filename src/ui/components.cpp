#include <algorithm>
#include <cmath>

#include "components.h"

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
        // focused: the accent fill; selected: the accent's dark tint; rest: a quiet elevated chip
        setFillColor(focused ? theme::accent() : selected ? theme::accentMuted() : theme::surfaceElevated());
        setOutlineThickness(0);
        caption->setColor(focused ? theme::onAccent() : selected ? theme::textStrong() : theme::textSecondary());
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
                // gear: a thick ring with eight teeth
                float body = s * 0.14f;
                auto *ring = new CircleShape(body);
                ring->setPointCount(28);
                ring->setOrigin(Origin::Center);
                ring->setPosition(c, c);
                addPart(ring, true);
                ring->setOutlineThickness(std::round(s * 0.065f));
                for (int i = 0; i < 8; i++) {
                    float a = (float) i * PI / 4;
                    auto *tooth = new RectangleShape(FloatRect(0, 0, s * 0.10f, s * 0.09f));
                    tooth->setOrigin(Origin::Center);
                    float R = s * 0.235f;
                    tooth->setPosition(c + R * std::cos(a), c + R * std::sin(a));
                    tooth->setRotation(a * 180 / PI);
                    addPart(tooth, false);
                }
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
        setFillColor(focused ? theme::accent() : theme::surfaceElevated());
        Color fg = focused ? theme::onAccent() : theme::textPrimary();
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

    TopNav::TopNav(const std::string &brandName, const std::vector<std::string> &sections, const std::vector<Icon> &icons)
            : RectangleShape(FloatRect(theme::SAFE_X, theme::SAFE_Y - 8, theme::CONTENT_W, 72)) {
        setFillColor(theme::none());
        const float h = getSize().y;
        auto *brand = new Brand(brandName, 40, theme::HEADING);
        brand->setPosition(0, std::round((h - 40) / 2));
        add(brand);
        float x = brand->getSize().x + theme::SPACE_XL;
        for (const std::string &s: sections) {
            auto *chip = new Chip(s, x, (h - theme::CHIP_H) / 2);
            add(chip);
            pills.push_back(chip);
            x += chip->width() + theme::SPACE_XS;
        }
        clock = new Label("", theme::HEADING, Weight::SemiBold, theme::textPrimary());
        clock->setAlign(Align::Right, getSize().x);
        clock->setPosition(0, Label::centerOffset(theme::HEADING, h));
        add(clock);
        // icons right-aligned before the clock ("00:00" is about 100 px at this size)
        float right = getSize().x - 140;
        for (size_t i = 0; i < icons.size(); i++) {
            auto *b = new IconButton(icons[i], theme::ICON_BUTTON);
            float bx = right - (float) (icons.size() - i) * (theme::ICON_BUTTON + theme::SPACE_S) + theme::SPACE_S;
            b->setPosition(bx, (h - theme::ICON_BUTTON) / 2);
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
        float x = std::min(p.x + theme::ICON_BUTTON / 2 - w / 2, getSize().x - w);
        iconCaption->setPosition(std::round(x), getSize().y + 6);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    EmptyState::EmptyState(const FloatRect &rect) : RectangleShape(rect) {
        setFillColor(theme::none());
        title = new Label("", theme::HEADING, Weight::SemiBold, theme::textPrimary());
        title->setAlign(Align::Center, rect.width);
        title->setMaxWidth(rect.width - 40);
        add(title);
        message = new Label("", theme::BODY, Weight::Regular, theme::textSecondary());
        message->setAlign(Align::Center, rect.width);
        message->setMaxWidth(std::min(rect.width - 40, 1100.0f));
        message->setMaxLines(3);
        add(message);
        action = new Label("", theme::LABEL, Weight::SemiBold, theme::accentText());
        action->setAlign(Align::Center, rect.width);
        action->setMaxWidth(rect.width - 40);
        add(action);
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
        float y = std::round(std::max(0.0f, (getSize().y - h) / 2));
        title->setPosition(0, y);
        y += title->height() + 16;
        message->setPosition(0, y);
        y += m.empty() ? 0 : message->height() + 24;
        action->setPosition(0, y);
        setVisibility(Visibility::Visible);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    Backdrop::Backdrop(const FloatRect &rect, float fadeLeft, float fadeBottom) : RectangleShape(rect) {
        setFillColor(theme::none());
        // no artwork: a faint glow of the accent's dark tint from the right edge
        glow = new GradientRectangle(FloatRect(0, 0, rect.width, rect.height));
        glow->setColor(theme::withAlpha(theme::accentMuted(), 120), theme::none(), GradientRectangle::Left);
        add(glow);
        image = new RectangleShape(FloatRect(0, 0, rect.width, rect.height));
        image->setFillColor(theme::backdropTint());   // texture modulation: the artwork itself is dimmed
        image->setVisibility(Visibility::Hidden);
        add(image);
        shade = new RectangleShape(FloatRect(0, 0, rect.width, rect.height));
        shade->setFillColor(theme::backdropShade());
        shade->setVisibility(Visibility::Hidden);
        add(shade);
        if (fadeLeft > 0) {
            auto *l = new GradientRectangle(FloatRect(0, 0, fadeLeft, rect.height));
            l->setColor(theme::background(), theme::withAlpha(theme::background(), 0), GradientRectangle::Right);
            add(l);
        }
        if (fadeBottom > 0) {
            auto *b = new GradientRectangle(FloatRect(0, rect.height - fadeBottom, rect.width, fadeBottom));
            b->setColor(theme::withAlpha(theme::background(), 0), theme::background(), GradientRectangle::Down);
            add(b);
        }
    }

    void Backdrop::set(const std::shared_ptr<Texture> &texture, const Vector2i &size) {
        bool has = texture != nullptr && size.x > 0 && size.y > 0;
        image->setVisibility(has ? Visibility::Visible : Visibility::Hidden);
        shade->setVisibility(has ? Visibility::Visible : Visibility::Hidden);
        glow->setVisibility(has ? Visibility::Hidden : Visibility::Visible);
        if (!has || texture == shown) {
            return;
        }
        image->setTexture(texture.get(), true);
        shown = texture;   // the previous texture is released only now
        // cover the box: crop the image's centre to the box's aspect ratio (never stretched)
        float boxAspect = getSize().x / getSize().y;
        float imgAspect = (float) size.x / (float) size.y;
        IntRect crop(0, 0, size.x, size.y);
        if (imgAspect > boxAspect) {
            crop.width = std::max(1, (int) std::round((float) size.y * boxAspect));
            crop.left = (size.x - crop.width) / 2;
        } else {
            crop.height = std::max(1, (int) std::round((float) size.x / boxAspect));
            crop.top = (size.y - crop.height) / 3;   // a little above the centre: faces, not feet
        }
        image->setTextureRect(crop);
        image->setSize(getSize().x, getSize().y);
        image->setPosition(0, 0);
    }
}
