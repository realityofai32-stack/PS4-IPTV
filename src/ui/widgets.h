// Reusable TV widgets built on libcross2d shapes and ui::Label.

#ifndef PS4IPTV_UI_WIDGETS_H
#define PS4IPTV_UI_WIDGETS_H

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cross2d/c2d.h"
#include "scroll_math.h"
#include "text.h"
#include "theme.h"

namespace ui {

    // Smooth scrolling of every ListView / GridView (Settings > Smooth scrolling). Off: lists move row by row.
    void setSmoothScrolling(bool on);

    bool smoothScrolling();

    // a list or grid is still gliding: the app keeps drawing frames until it settles
    bool scrolling(double now);

    // rounded box added to parent
    c2d::RectangleShape *box(c2d::C2DObject *parent, const c2d::FloatRect &rect, const c2d::Color &color,
                             float radius = theme::RADIUS);

    Label *label(c2d::C2DObject *parent, const std::string &text, unsigned size, float x, float y,
                 Weight weight = Weight::Regular, c2d::Color color = theme::textPrimary());

    // full-screen vertical gradient background
    c2d::C2DObject *background(c2d::C2DObject *parent);

    enum class Glyph {
        Cross,
        Circle,
        Square,
        Triangle,
        L1,
        R1,
        L2,
        R2,
        Options,
        DPad
    };

    // a DualShock button symbol (vector shapes; the UI font has no PlayStation glyphs)
    class ButtonGlyph : public c2d::RectangleShape {
    public:
        ButtonGlyph(Glyph glyph, float size);
    };

    // bottom controller hints: [glyph] text  [glyph] text ...
    class HintBar : public c2d::RectangleShape {
    public:
        HintBar();

        void setHints(const std::vector<std::pair<Glyph, std::string>> &hints);

    private:
        std::vector<std::pair<Glyph, std::string>> current;
    };

    class Button : public c2d::RectangleShape {
    public:
        Button(const std::string &text, const c2d::FloatRect &rect, bool primary = false);

        void setFocused(bool focused);

        void setEnabled(bool enabled);

        bool isEnabled() const { return enabled; }

        void setText(const std::string &text);

    private:
        void refresh();

        void fit();

        Label *caption;
        bool focused = false;
        bool enabled = true;
        bool primary;
    };

    // initials tile used where a logo/poster is missing or still loading
    class Monogram : public c2d::RectangleShape {
    public:
        Monogram(float size, unsigned fontSize);

        void setName(const std::string &name);

        static std::string initials(const std::string &name);

        // stable colour for a name's initials tile
        static c2d::Color color(const std::string &name);

    private:
        Label *letters;
        std::string current;
    };

    // Channel logo in a rounded tile: the image centred with its aspect ratio kept (never stretched), on a
    // neutral tile so transparent logos stay readable. Without an image it shows the initials placeholder.
    // The widget holds a reference to the texture it shows, so a texture leaving the image cache is only
    // released once this widget shows something else.
    class LogoView : public c2d::RectangleShape {
    public:
        // padding: space between the tile edge and the image box; maxUpscale: how far a small image may
        // be enlarged to fill the box (1 = never)
        LogoView(const c2d::FloatRect &rect, unsigned initialsSize, float padding, float maxUpscale);

        // texture null: initials placeholder. imageSize: the image part of the (power-of-two) texture.
        void set(const std::string &name, const std::shared_ptr<c2d::Texture> &texture,
                 const c2d::Vector2i &imageSize);

    private:
        c2d::RectangleShape *image;
        Label *initials;
        std::shared_ptr<c2d::Texture> shown;
        std::string currentName;
        float padding;
        float maxUpscale;
        bool showingImage = false;
    };

    // Thin rounded vertical scrollbar: translucent track, thumb sized visible/total and positioned
    // first/(total-visible). Hidden when everything fits.
    class ScrollBar : public c2d::RectangleShape {
    public:
        static constexpr float WIDTH = 6;
        static constexpr float GAP = 16;     // space between list content and the bar

        ScrollBar(float x, float y, float height);

        void setRange(int total, int visible, int first);

        // brighter thumb while the list has focus
        void setActive(bool active);

    private:
        c2d::RectangleShape *thumb;
        bool active = true;
    };

    // three pulsing dots; call tick() each frame while visible
    class Spinner : public c2d::RectangleShape {
    public:
        explicit Spinner(float size);

        // returns true when the visuals changed (caller requests a redraw)
        bool tick(double now);

    private:
        c2d::CircleShape *dots[3];
        int phase = -1;
    };

    // Movie poster / series cover in a rounded tile. Images close to the tile's aspect ratio fill it
    // (cropped evenly, never stretched); others are fitted inside it. Without an image the title is shown.
    // Optional progress bar (partially watched) and a "watched" badge.
    class PosterView : public c2d::RectangleShape {
    public:
        PosterView(const c2d::FloatRect &rect, unsigned fallbackTextSize);

        void set(const std::string &title, const std::shared_ptr<c2d::Texture> &texture,
                 const c2d::Vector2i &imageSize);

        // 0 hides the bar
        void setProgress(float fraction);

        void setWatched(bool watched);

        // a small corner mark: the movie / series has downloaded content (playable offline)
        void setDownloaded(bool downloaded);

        void setFocused(bool focused);

    private:
        c2d::RectangleShape *image;
        Label *fallback;
        c2d::RectangleShape *barTrack;
        c2d::RectangleShape *barFill;
        c2d::CircleShape *badge;
        Label *badgeMark;
        c2d::CircleShape *downloadBadge;
        std::shared_ptr<c2d::Texture> shown;
        std::string currentTitle;
        bool showingImage = false;
        bool lifted = false;
    };

    // Virtualized grid: only the cells on screen (+ one row) exist and are re-bound when scrolling by whole
    // rows. Spacing is derived from the rect; a ScrollBar sits in the right gutter. Focus moves at once; the
    // drawn rows glide after it (scroll::Smooth), clipped to the grid while they do.
    class GridView : public c2d::RectangleShape {
    public:
        struct Adapter {
            virtual ~Adapter() = default;

            virtual int count() = 0;

            virtual c2d::C2DObject *createCell(float width, float height) = 0;

            virtual void bindCell(c2d::C2DObject *cell, int index, bool focused) = 0;
        };

        GridView(const c2d::FloatRect &rect, float cellWidth, float cellHeight, int columns, int rows,
                 Adapter *adapter);

        void reload();

        void setSelected(int index);

        int selected() const { return sel; }

        // false when the move hits an edge (nothing changed): the screen may move focus elsewhere
        bool navigate(int dx, int dy);

        bool page(int pages);

        void setFocused(bool focused);

        int columns() const { return cols; }

        int visibleRows() const { return rows; }

        // first item on screen and how many cells there are (for image requests)
        int firstVisible() const { return firstRow * cols; }

        int cellCount() const { return cols * rows; }

    protected:
        void onUpdate() override;

        void onDraw(c2d::Transform &transform, bool draw) override;

    private:
        // animate: glide from the drawn position (moves); otherwise jump there (reloads, new lists)
        void layout(bool animate);

        void place();

        Adapter *adapter;
        std::vector<c2d::C2DObject *> cells;
        ScrollBar *bar;
        int cols;
        int rows;
        int sel = 0;
        int firstRow = 0;
        bool focus = true;
        float cellWidth = 0;
        float cellHeight = 0;
        float gapX = 0;
        float pitchY = 0;
        float extent = 0;
        scroll::Smooth smooth;
        int boundBase = -1;
        double lastUpdate = -1;
    };

    // Virtualized vertical list: only the visible rows (+ one) exist; rows are re-bound when scrolling. Focus
    // moves at once; the drawn rows glide after it (scroll::Smooth), clipped to the list while they do.
    class ListView : public c2d::RectangleShape {
    public:
        struct Adapter {
            virtual ~Adapter() = default;

            virtual int count() = 0;

            // create one reusable row object of the given size
            virtual c2d::C2DObject *createRow(float width, float height) = 0;

            // fill a row for item `index`; focused = this row is selected and the list has focus
            virtual void bindRow(c2d::C2DObject *row, int index, bool selected, bool focused) = 0;
        };

        ListView(const c2d::FloatRect &rect, float rowHeight, float spacing, Adapter *adapter);

        // call after the data changed; keeps the selection when possible
        void reload();

        void setSelected(int index);

        int selected() const { return sel; }

        // moves the selection; returns false at the ends (no wrap)
        bool moveSelection(int delta);

        void setFocused(bool focused);

        bool isFocused() const { return focus; }

        int visibleCount() const { return visibleRows; }

        // index of the first row on screen
        int firstVisible() const { return first; }

        // rows moved by a page jump (L2/R2): one screen minus one row of context
        int pageSize() const { return std::max(1, visibleRows - 1); }

        // width available to rows (the scrollbar gutter is reserved on the right)
        static float rowWidth(float listWidth) { return listWidth - ScrollBar::WIDTH - ScrollBar::GAP; }

    protected:
        void onUpdate() override;

        void onDraw(c2d::Transform &transform, bool draw) override;

    private:
        void layout(bool animate);

        void place();

        Adapter *adapter;
        std::vector<c2d::C2DObject *> rows;
        ScrollBar *bar = nullptr;
        int visibleRows = 1;
        int sel = 0;
        int first = 0;
        bool focus = true;
        float rowH = 0;
        float pitch = 0;
        float extent = 0;
        scroll::Smooth smooth;
        int boundBase = -1;
        double lastUpdate = -1;
    };
}

#endif // PS4IPTV_UI_WIDGETS_H
