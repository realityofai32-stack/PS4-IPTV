#include <algorithm>
#include <cmath>

#include "widgets.h"

using namespace c2d;

namespace ui {

    RectangleShape *box(C2DObject *parent, const FloatRect &rect, const Color &color, float radius) {
        auto *r = new RectangleShape(rect);
        r->setFillColor(color);
        if (radius > 0) {
            r->setCornersRadius(radius);
            r->setCornerPointCount(8);
        }
        if (parent) {
            parent->add(r);
        }
        return r;
    }

    Label *label(C2DObject *parent, const std::string &text, unsigned size, float x, float y, Weight weight,
                 Color color) {
        auto *l = new Label(text, size, weight, color);
        l->setPosition(x, y);
        if (parent) {
            parent->add(l);
        }
        return l;
    }

    C2DObject *background(C2DObject *parent) {
        auto *g = new GradientRectangle(FloatRect(0, 0, theme::SCREEN_W, theme::SCREEN_H));
        g->setColor(theme::bgTop(), theme::bgBottom(), GradientRectangle::Down);
        if (parent) {
            parent->add(g);
        }
        return g;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    ButtonGlyph::ButtonGlyph(Glyph glyph, float s) : RectangleShape(FloatRect(0, 0, s, s)) {
        setFillColor(Color::Transparent);
        const Color fg = theme::text();
        const float t = std::max(2.0f, std::round(s * 0.09f));
        auto *cap = new CircleShape(s / 2.0f);
        cap->setFillColor(theme::surfaceRaised());
        cap->setPointCount(24);
        add(cap);
        const float c = s / 2.0f;
        switch (glyph) {
            case Glyph::Cross: {
                for (float angle: {45.0f, -45.0f}) {
                    auto *bar = new RectangleShape(FloatRect(c, c, s * 0.46f, t));
                    bar->setOrigin(Origin::Center);
                    bar->setRotation(angle);
                    bar->setFillColor(Color(125, 170, 240));
                    add(bar);
                }
                break;
            }
            case Glyph::Circle: {
                auto *ring = new CircleShape(s * 0.22f);
                ring->setOrigin(Origin::Center);
                ring->setPosition(c, c);
                ring->setFillColor(Color::Transparent);
                ring->setOutlineColor(Color(240, 110, 120));
                ring->setOutlineThickness(t);
                ring->setPointCount(24);
                add(ring);
                break;
            }
            case Glyph::Square: {
                auto *sq = new RectangleShape(FloatRect(c, c, s * 0.40f, s * 0.40f));
                sq->setOrigin(Origin::Center);
                sq->setFillColor(Color::Transparent);
                sq->setOutlineColor(Color(230, 140, 220));
                sq->setOutlineThickness(t);
                add(sq);
                break;
            }
            case Glyph::Triangle: {
                auto *tri = new ConvexShape(3);
                float r = s * 0.25f;
                tri->setPoint(0, Vector2f(c, c - r));
                tri->setPoint(1, Vector2f(c + r * 0.95f, c + r * 0.62f));
                tri->setPoint(2, Vector2f(c - r * 0.95f, c + r * 0.62f));
                tri->setFillColor(Color::Transparent);
                tri->setOutlineColor(Color(80, 210, 180));
                tri->setOutlineThickness(t);
                add(tri);
                break;
            }
            default: {
                // shoulder / options buttons: a text pill instead of a round cap
                remove(cap);
                delete cap;
                const char *txt = glyph == Glyph::L1 ? "L1" : glyph == Glyph::R1 ? "R1" : glyph == Glyph::L2 ? "L2"
                                  : glyph == Glyph::R2 ? "R2" : glyph == Glyph::DPad ? "D-PAD" : "OPTIONS";
                auto *l = new Label(txt, (unsigned) std::round(s * 0.42f), Weight::SemiBold, fg);
                float w = l->width() + s * 0.6f;
                setSize(w, s);
                auto *pill = box(nullptr, FloatRect(0, 0, w, s), theme::surfaceRaised(), s / 2.0f);
                add(pill);
                l->setAlign(Align::Center, w);
                l->setPosition(0, Label::centerOffset((unsigned) std::round(s * 0.42f), s));
                add(l);
                break;
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    HintBar::HintBar() : RectangleShape(FloatRect(theme::SAFE_X, theme::SCREEN_H - theme::SAFE_Y - 40,
                                                  theme::SCREEN_W - 2 * theme::SAFE_X, 40)) {
        setFillColor(Color::Transparent);
    }

    void HintBar::setHints(const std::vector<std::pair<Glyph, std::string>> &hints) {
        if (hints == current) {
            return;
        }
        current = hints;
        for (auto *child: getChilds()) {
            remove(child);
            delete child;
        }
        float x = 0;
        for (const auto &h: hints) {
            auto *g = new ButtonGlyph(h.first, 36);
            g->setPosition(x, 2);
            add(g);
            x += g->getSize().x + 12;
            auto *l = label(this, h.second, theme::LABEL, x, Label::centerOffset(theme::LABEL, 40), Weight::Regular,
                            theme::textDim());
            x += l->width() + 40;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    Button::Button(const std::string &text, const FloatRect &rect, bool isPrimary)
            : RectangleShape(rect), primary(isPrimary) {
        setCornersRadius(rect.height / 2.0f);
        setCornerPointCount(10);
        caption = new Label(text, theme::BODY, Weight::SemiBold, theme::text());
        caption->setAlign(Align::Center, rect.width);
        caption->setMaxWidth(rect.width - 32);
        caption->setPosition(0, Label::centerOffset(theme::BODY, rect.height));
        add(caption);
        refresh();
    }

    void Button::setFocused(bool f) {
        focused = f;
        refresh();
    }

    void Button::setEnabled(bool e) {
        enabled = e;
        refresh();
    }

    void Button::setText(const std::string &text) {
        caption->setText(text);
    }

    void Button::refresh() {
        if (!enabled) {
            setFillColor(theme::surface());
            setOutlineThickness(0);
            caption->setColor(theme::textMuted());
        } else if (focused) {
            setFillColor(theme::accent());
            setOutlineColor(theme::withAlpha(Color::White, 230));
            setOutlineThickness(3);
            caption->setColor(Color::White);
        } else {
            setFillColor(primary ? theme::accentDark() : theme::surfaceRaised());
            setOutlineThickness(0);
            caption->setColor(theme::text());
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    Spinner::Spinner(float size) : RectangleShape(FloatRect(0, 0, size * 3.6f, size)) {
        setFillColor(Color::Transparent);
        for (int i = 0; i < 3; i++) {
            dots[i] = new CircleShape(size / 2.0f);
            dots[i]->setPointCount(16);
            dots[i]->setPosition((float) i * size * 1.3f, 0);
            dots[i]->setFillColor(theme::textMuted());
            add(dots[i]);
        }
    }

    bool Spinner::tick(double now) {
        int p = (int) (now / 0.12) % 6;  // ~8 visual steps per second: cheap redraws
        if (p == phase) {
            return false;
        }
        phase = p;
        for (int i = 0; i < 3; i++) {
            bool lit = (p % 3) == i;
            dots[i]->setFillColor(lit ? theme::accent() : theme::textMuted());
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    ListView::ListView(const FloatRect &rect, float rowHeight, float spacing, Adapter *a)
            : RectangleShape(rect), adapter(a) {
        setFillColor(Color::Transparent);
        int n = std::max(1, (int) ((rect.height + spacing) / (rowHeight + spacing)));
        for (int i = 0; i < n; i++) {
            C2DObject *row = adapter->createRow(rect.width - 18, rowHeight);
            auto *t = (Transformable *) row;
            t->setPosition(0, (float) i * (rowHeight + spacing));
            add(row);
            rows.push_back(row);
        }
        scrollTrack = box(this, FloatRect(rect.width - 6, 0, 6, rect.height), theme::surface(), 3);
        scrollThumb = box(this, FloatRect(rect.width - 6, 0, 6, 40), theme::textMuted(), 3);
        reload();
    }

    void ListView::reload() {
        int count = adapter->count();
        sel = count == 0 ? 0 : std::min(std::max(sel, 0), count - 1);
        layout();
    }

    void ListView::setSelected(int index) {
        sel = index;
        reload();
    }

    bool ListView::moveSelection(int delta) {
        int count = adapter->count();
        if (count == 0) {
            return false;
        }
        int target = std::min(std::max(sel + delta, 0), count - 1);
        if (target == sel) {
            return false;
        }
        sel = target;
        layout();
        return true;
    }

    void ListView::setFocused(bool f) {
        if (f != focus) {
            focus = f;
            layout();
        }
    }

    void ListView::layout() {
        int count = adapter->count();
        int visible = (int) rows.size();
        // keep one row of context above/below the selection while scrolling
        int margin = visible >= 5 ? 1 : 0;
        if (sel < first + margin) {
            first = std::max(0, sel - margin);
        } else if (sel > first + visible - 1 - margin) {
            first = sel - (visible - 1 - margin);
        }
        first = std::max(0, std::min(first, std::max(0, count - visible)));

        for (int i = 0; i < visible; i++) {
            int index = first + i;
            auto *row = rows[(size_t) i];
            if (index < count) {
                row->setVisibility(Visibility::Visible);
                adapter->bindRow(row, index, index == sel, focus && index == sel);
            } else {
                row->setVisibility(Visibility::Hidden);
            }
        }

        bool scrollable = count > visible;
        scrollTrack->setVisibility(scrollable ? Visibility::Visible : Visibility::Hidden);
        scrollThumb->setVisibility(scrollable ? Visibility::Visible : Visibility::Hidden);
        if (scrollable) {
            float h = getSize().y;
            float thumb = std::max(40.0f, h * (float) visible / (float) count);
            float pos = (h - thumb) * (float) first / (float) (count - visible);
            scrollThumb->setSize(6, thumb);
            scrollThumb->setPosition(getSize().x - 6, pos);
        }
    }
}
