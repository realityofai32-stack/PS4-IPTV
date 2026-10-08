#include <algorithm>
#include <cmath>

#include "scroll_math.h"
#include "widgets.h"
#include "../core/utf8.h"

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
        g->setColor(theme::background(), theme::backgroundSecondary(), GradientRectangle::Down);
        if (parent) {
            parent->add(g);
        }
        return g;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    ButtonGlyph::ButtonGlyph(Glyph glyph, float s) : RectangleShape(FloatRect(0, 0, s, s)) {
        setFillColor(theme::none());
        const Color fg = theme::textPrimary();
        const float t = std::max(2.0f, std::round(s * 0.09f));
        auto *cap = new CircleShape(s / 2.0f);
        cap->setFillColor(theme::surfaceElevated());
        cap->setPointCount(24);
        add(cap);
        const float c = s / 2.0f;
        switch (glyph) {
            case Glyph::Cross: {
                for (float angle: {45.0f, -45.0f}) {
                    auto *bar = new RectangleShape(FloatRect(c, c, s * 0.46f, t));
                    bar->setOrigin(Origin::Center);
                    bar->setRotation(angle);
                    bar->setFillColor(theme::padCross());
                    add(bar);
                }
                break;
            }
            case Glyph::Circle: {
                auto *ring = new CircleShape(s * 0.22f);
                ring->setOrigin(Origin::Center);
                ring->setPosition(c, c);
                ring->setFillColor(theme::none());
                ring->setOutlineColor(theme::padCircle());
                ring->setOutlineThickness(t);
                ring->setPointCount(24);
                add(ring);
                break;
            }
            case Glyph::Square: {
                auto *sq = new RectangleShape(FloatRect(c, c, s * 0.40f, s * 0.40f));
                sq->setOrigin(Origin::Center);
                sq->setFillColor(theme::none());
                sq->setOutlineColor(theme::padSquare());
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
                tri->setFillColor(theme::none());
                tri->setOutlineColor(theme::padTriangle());
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
                auto *pill = box(nullptr, FloatRect(0, 0, w, s), theme::surfaceElevated(), s / 2.0f);
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
        setFillColor(theme::none());
    }

    void HintBar::setHints(const std::vector<std::pair<Glyph, std::string>> &hints) {
        if (hints == current) {
            return;
        }
        current = hints;
        // regular spacing first; a compact layout when the hints do not fit (longer translations)
        const struct {
            unsigned size;
            float gap;
        } layouts[] = {{theme::LABEL, 40}, {theme::CAPTION, 22}};
        for (const auto &layout: layouts) {
            for (auto *child: getChilds()) {
                remove(child);
                delete child;
            }
            float x = 0;
            for (const auto &h: hints) {
                auto *g = new ButtonGlyph(h.first, 36);
                g->setPosition(x, 2);
                add(g);
                x += g->getSize().x + 10;
                auto *l = label(this, h.second, layout.size, x, Label::centerOffset(layout.size, 40), Weight::Regular,
                                theme::textSecondary());
                x += l->width() + layout.gap;
            }
            if (x - layout.gap <= getSize().x) {
                break;
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    Button::Button(const std::string &text, const FloatRect &rect, bool isPrimary)
            : RectangleShape(rect), primary(isPrimary) {
        setCornersRadius(rect.height / 2.0f);
        setCornerPointCount(10);
        caption = new Label(text, theme::BODY, Weight::SemiBold, theme::textPrimary());
        caption->setAlign(Align::Center, rect.width);
        add(caption);
        fit();
        refresh();
    }

    // longer captions (translations, "Resume offline 1:02:15") get a smaller size before being shortened
    void Button::fit() {
        float room = getSize().x - 32;
        caption->setMaxWidth(0);
        unsigned size = theme::BODY;
        for (unsigned s: {theme::BODY, theme::LABEL, theme::CAPTION}) {
            size = s;
            caption->setCharSize(s);
            if (caption->width() <= room) {
                break;
            }
        }
        caption->setMaxWidth(room);
        caption->setPosition(0, Label::centerOffset(size, getSize().y));
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
        if (text != caption->getText()) {
            caption->setText(text);
            fit();
        }
    }

    void Button::refresh() {
        if (!enabled) {
            setFillColor(theme::surface());
            setOutlineThickness(0);
            caption->setColor(theme::textMuted());
        } else if (focused) {
            setFillColor(theme::accent());   // the accent fill alone marks it: no outline
            setOutlineThickness(0);
            caption->setColor(theme::textStrong());
        } else {
            setFillColor(primary ? theme::accentMuted() : theme::surfaceElevated());
            setOutlineThickness(0);
            caption->setColor(theme::textPrimary());
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    Monogram::Monogram(float size, unsigned fontSize) : RectangleShape(FloatRect(0, 0, size, size)) {
        setCornersRadius(size * 0.18f);
        setCornerPointCount(8);
        letters = new Label("", fontSize, Weight::SemiBold, theme::textStrong());
        letters->setAlign(Align::Center, size);
        letters->setPosition(0, Label::centerOffset(fontSize, size));
        add(letters);
        setFillColor(theme::surfaceElevated());
    }

    std::string Monogram::initials(const std::string &name) {
        std::u32string s = utf8::decode(name);
        std::vector<std::u32string> words;
        std::u32string w;
        for (char32_t c: s) {
            bool sep = c == U' ' || c == U'|' || c == U'-' || c == U'_' || c == U'.' || c == U'(' || c == U')'
                       || c == U'[' || c == U']';
            if (sep) {
                if (!w.empty()) {
                    words.push_back(w);
                }
                w.clear();
            } else {
                w.push_back(c);
            }
        }
        if (!w.empty()) {
            words.push_back(w);
        }
        // drop a country/provider prefix such as "TR:" or "UK:"
        if (words.size() > 1 && words[0].size() <= 4 && words[0].back() == U':') {
            words.erase(words.begin());
        }
        std::string out;
        for (size_t i = 0; i < words.size() && i < 2; i++) {
            char32_t c = words[i][0];
            if (c == U':') {
                continue;
            }
            if (c >= 'a' && c <= 'z') {
                c -= 32;
            }
            out += utf8::encode(c);
        }
        return out.empty() ? "?" : out;
    }

    Color Monogram::color(const std::string &name) {
        uint32_t h = 2166136261u;
        for (unsigned char c: name) {
            h = (h ^ c) * 16777619u;
        }
        return theme::monogram(h);
    }

    void Monogram::setName(const std::string &name) {
        if (name == current) {
            return;
        }
        current = name;
        letters->setText(initials(name));
        setFillColor(color(name));
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    LogoView::LogoView(const FloatRect &rect, unsigned initialsSize, float pad, float upscale)
            : RectangleShape(rect), padding(pad), maxUpscale(upscale) {
        setCornersRadius(std::min(rect.width, rect.height) * 0.16f);
        setCornerPointCount(8);
        setFillColor(theme::logoTile());
        image = new RectangleShape(FloatRect(0, 0, 1, 1));
        image->setFillColor(theme::untinted());
        image->setVisibility(Visibility::Hidden);
        add(image);
        initials = new Label("", initialsSize, Weight::SemiBold, theme::textStrong());
        initials->setAlign(Align::Center, rect.width);
        initials->setMaxWidth(rect.width - 8);
        initials->setPosition(0, Label::centerOffset(initialsSize, rect.height));
        add(initials);
    }

    void LogoView::set(const std::string &name, const std::shared_ptr<Texture> &texture, const Vector2i &size) {
        bool hasImage = texture != nullptr && size.x > 0 && size.y > 0;
        if (name == currentName && hasImage == showingImage && (!hasImage || texture == shown)) {
            return;   // unchanged: rows are rebound on every move, keep that free
        }
        currentName = name;
        showingImage = hasImage;
        if (!hasImage) {
            // the hidden image shape still points at `shown`, which stays referenced until replaced
            image->setVisibility(Visibility::Hidden);
            initials->setText(Monogram::initials(name));
            initials->setVisibility(Visibility::Visible);
            setFillColor(Monogram::color(name));
            return;
        }
        if (texture != shown) {
            image->setTexture(texture.get(), true);
            image->setTextureRect(IntRect(0, 0, size.x, size.y));
            shown = texture;   // the previous texture is released only now
        }
        // fit the image box, aspect ratio kept, centred
        float boxW = getSize().x - 2 * padding;
        float boxH = getSize().y - 2 * padding;
        float scale = std::min(std::min(boxW / (float) size.x, boxH / (float) size.y), maxUpscale);
        float w = std::round((float) size.x * scale);
        float h = std::round((float) size.y * scale);
        image->setSize(w, h);
        image->setPosition(std::round((getSize().x - w) / 2), std::round((getSize().y - h) / 2));
        image->setVisibility(Visibility::Visible);
        initials->setVisibility(Visibility::Hidden);
        setFillColor(theme::logoTile());
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    ScrollBar::ScrollBar(float x, float y, float height) : RectangleShape(FloatRect(x, y, WIDTH, height)) {
        setCornersRadius(WIDTH / 2);
        setCornerPointCount(6);
        setFillColor(theme::scrollTrack());
        thumb = box(this, FloatRect(0, 0, WIDTH, WIDTH * 8), theme::scrollThumb(), WIDTH / 2);
        thumb->setCornerPointCount(6);
        setVisibility(Visibility::Hidden);
    }

    void ScrollBar::setRange(int total, int visible, int first) {
        scroll::Thumb t = scroll::thumb(getSize().y, total, visible, first, 48);
        setVisibility(t.visible ? Visibility::Visible : Visibility::Hidden);
        if (t.visible) {
            thumb->setSize(WIDTH, std::round(t.length));
            thumb->setPosition(0, std::round(t.offset));
        }
    }

    void ScrollBar::setActive(bool a) {
        active = a;
        thumb->setFillColor(active ? theme::scrollThumb() : theme::scrollThumbIdle());
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    Spinner::Spinner(float size) : RectangleShape(FloatRect(0, 0, size * 3.6f, size)) {
        setFillColor(theme::none());
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

    PosterView::PosterView(const FloatRect &rect, unsigned textSize) : RectangleShape(rect) {
        setCornersRadius(theme::RADIUS_SMALL);
        setCornerPointCount(8);
        setFillColor(theme::surfaceElevated());
        image = new RectangleShape(FloatRect(0, 0, 1, 1));
        image->setFillColor(theme::untinted());
        image->setVisibility(Visibility::Hidden);
        add(image);
        fallback = new Label("", textSize, Weight::SemiBold, theme::textSecondary());
        fallback->setAlign(Align::Center, rect.width);
        fallback->setMaxWidth(rect.width - 20);
        fallback->setMaxLines(4);
        add(fallback);
        barTrack = box(this, FloatRect(10, rect.height - 16, rect.width - 20, 6), theme::progressTrackOnImage(), 3);
        barFill = box(barTrack, FloatRect(0, 0, 1, 6), theme::accent(), 3);
        barTrack->setVisibility(Visibility::Hidden);
        float r = std::max(12.0f, rect.width * 0.09f);
        badge = new CircleShape(r);
        badge->setPointCount(20);
        badge->setFillColor(theme::success());
        badge->setPosition(rect.width - 2 * r - 8, 8);
        add(badge);
        badgeMark = new Label("\xE2\x9C\x93", (unsigned) (r * 1.3f), Weight::SemiBold, theme::textStrong());  // check mark
        badgeMark->setAlign(Align::Center, 2 * r);
        badgeMark->setPosition(0, Label::centerOffset((unsigned) (r * 1.3f), 2 * r));
        badge->add(badgeMark);
        badge->setVisibility(Visibility::Hidden);
        downloadBadge = new CircleShape(r);
        downloadBadge->setPointCount(20);
        downloadBadge->setFillColor(theme::overlayPanel());
        downloadBadge->setOutlineColor(theme::accent());
        downloadBadge->setOutlineThickness(2);
        downloadBadge->setPosition(8, 8);
        add(downloadBadge);
        auto *arrow = new Label("\xE2\x86\x93", (unsigned) (r * 1.3f), Weight::SemiBold, theme::accentText());  // down arrow
        arrow->setAlign(Align::Center, 2 * r);
        arrow->setPosition(0, Label::centerOffset((unsigned) (r * 1.3f), 2 * r));
        downloadBadge->add(arrow);
        downloadBadge->setVisibility(Visibility::Hidden);
    }

    void PosterView::setDownloaded(bool downloaded) {
        downloadBadge->setVisibility(downloaded ? Visibility::Visible : Visibility::Hidden);
    }

    void PosterView::set(const std::string &title, const std::shared_ptr<Texture> &texture, const Vector2i &size) {
        bool hasImage = texture != nullptr && size.x > 0 && size.y > 0;
        if (title == currentTitle && hasImage == showingImage && (!hasImage || texture == shown)) {
            return;
        }
        currentTitle = title;
        showingImage = hasImage;
        if (!hasImage) {
            image->setVisibility(Visibility::Hidden);   // keeps pointing at `shown`, still referenced
            fallback->setText(title);
            fallback->setPosition(0, std::round((getSize().y - fallback->height()) / 2));
            fallback->setVisibility(Visibility::Visible);
            return;
        }
        if (texture != shown) {
            image->setTexture(texture.get(), true);
            shown = texture;
        }
        float tw = getSize().x;
        float th = getSize().y;
        float tileAspect = tw / th;
        float imgAspect = (float) size.x / (float) size.y;
        if (std::fabs(imgAspect / tileAspect - 1.0f) < 0.15f) {
            // close to the tile shape: fill it, cropping the longer side evenly (no distortion)
            IntRect crop(0, 0, size.x, size.y);
            if (imgAspect > tileAspect) {
                crop.width = (int) std::round((float) size.y * tileAspect);
                crop.left = (size.x - crop.width) / 2;
            } else {
                crop.height = (int) std::round((float) size.x / tileAspect);
                crop.top = (size.y - crop.height) / 2;
            }
            image->setTextureRect(crop);
            image->setSize(tw, th);
            image->setPosition(0, 0);
        } else {
            image->setTextureRect(IntRect(0, 0, size.x, size.y));
            float scale = std::min(tw / (float) size.x, th / (float) size.y);
            float w = std::round((float) size.x * scale);
            float h = std::round((float) size.y * scale);
            image->setSize(w, h);
            image->setPosition(std::round((tw - w) / 2), std::round((th - h) / 2));
        }
        image->setVisibility(Visibility::Visible);
        fallback->setVisibility(Visibility::Hidden);
    }

    void PosterView::setProgress(float fraction) {
        bool show = fraction > 0.001f;
        barTrack->setVisibility(show ? Visibility::Visible : Visibility::Hidden);
        if (show) {
            float w = barTrack->getSize().x;
            barFill->setSize(std::max(6.0f, std::round(w * std::min(1.0f, fraction))), 6);
        }
    }

    void PosterView::setWatched(bool watched) {
        badge->setVisibility(watched ? Visibility::Visible : Visibility::Hidden);
    }

    void PosterView::setFocused(bool focused) {
        if (focused == lifted && getOutlineThickness() == (focused ? theme::FOCUS_BORDER : 0)) {
            return;
        }
        setOutlineColor(theme::accent());
        setOutlineThickness(focused ? theme::FOCUS_BORDER : 0);
        // a focused poster rises a little: a position change, no animation and no shadow
        Vector2f p = getPosition();
        p.y += (lifted ? theme::FOCUS_LIFT : 0) - (focused ? theme::FOCUS_LIFT : 0);
        setPosition(p);
        lifted = focused;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    GridView::GridView(const FloatRect &rect, float cellW, float cellH, int columns, int visibleRows, Adapter *a)
            : RectangleShape(rect), adapter(a), cols(std::max(1, columns)), rows(std::max(1, visibleRows)) {
        setFillColor(theme::none());
        float usableW = rect.width - ScrollBar::WIDTH - ScrollBar::GAP;
        float gapX = cols > 1 ? std::max(0.0f, (usableW - (float) cols * cellW) / (float) (cols - 1)) : 0;
        float gapY = rows > 1 ? std::max(0.0f, (rect.height - (float) rows * cellH) / (float) (rows - 1)) : 0;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                C2DObject *cell = adapter->createCell(cellW, cellH);
                ((Transformable *) cell)->setPosition(std::round((float) c * (cellW + gapX)),
                                                      std::round((float) r * (cellH + gapY)));
                add(cell);
                cells.push_back(cell);
            }
        }
        bar = new ScrollBar(rect.width - ScrollBar::WIDTH, 0, rect.height);
        add(bar);
        reload();
    }

    void GridView::reload() {
        int count = adapter->count();
        sel = count == 0 ? 0 : std::min(std::max(sel, 0), count - 1);
        layout();
    }

    void GridView::setSelected(int index) {
        sel = index;
        reload();
    }

    bool GridView::navigate(int dx, int dy) {
        int target = scroll::gridMove(sel, dx, dy, cols, adapter->count());
        if (target < 0 || target == sel) {
            return false;
        }
        sel = target;
        layout();
        return true;
    }

    bool GridView::page(int pages) {
        int target = scroll::gridPage(sel, pages, rows, cols, adapter->count());
        if (target == sel) {
            return false;
        }
        sel = target;
        layout();
        return true;
    }

    void GridView::setFocused(bool f) {
        if (f != focus) {
            focus = f;
            bar->setActive(focus);
            layout();
        }
    }

    void GridView::layout() {
        int count = adapter->count();
        int totalRows = count == 0 ? 0 : (count - 1) / cols + 1;
        // keep one row of context above/below while scrolling (selection in the middle row of three)
        firstRow = scroll::firstVisible(sel / cols, firstRow, rows, totalRows, rows >= 3 ? 1 : 0);
        for (int i = 0; i < (int) cells.size(); i++) {
            int index = firstRow * cols + i;
            C2DObject *cell = cells[(size_t) i];
            if (index < count) {
                cell->setVisibility(Visibility::Visible);
                adapter->bindCell(cell, index, focus && index == sel);
            } else {
                cell->setVisibility(Visibility::Hidden);
            }
        }
        bar->setRange(totalRows, rows, firstRow);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////

    ListView::ListView(const FloatRect &rect, float rowHeight, float spacing, Adapter *a)
            : RectangleShape(rect), adapter(a) {
        setFillColor(theme::none());
        int n = std::max(1, (int) ((rect.height + spacing) / (rowHeight + spacing)));
        for (int i = 0; i < n; i++) {
            C2DObject *row = adapter->createRow(rowWidth(rect.width), rowHeight);
            auto *t = (Transformable *) row;
            t->setPosition(0, (float) i * (rowHeight + spacing));
            add(row);
            rows.push_back(row);
        }
        // the bar spans exactly the rows, not the leftover space below the last one
        bar = new ScrollBar(rect.width - ScrollBar::WIDTH, 0, (float) n * rowHeight + (float) (n - 1) * spacing);
        add(bar);
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
            bar->setActive(focus);
            layout();
        }
    }

    void ListView::layout() {
        int count = adapter->count();
        int visible = (int) rows.size();
        // keep one row of context above/below the selection while scrolling
        first = scroll::firstVisible(sel, first, visible, count, visible >= 5 ? 1 : 0);

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

        bar->setRange(count, visible, first);
    }
}
