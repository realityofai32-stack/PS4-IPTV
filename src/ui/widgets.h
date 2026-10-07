// Reusable TV widgets built on libcross2d shapes and ui::Label.

#ifndef PS4IPTV_UI_WIDGETS_H
#define PS4IPTV_UI_WIDGETS_H

#include <functional>
#include <string>
#include <vector>

#include "cross2d/c2d.h"
#include "text.h"
#include "theme.h"

namespace ui {

    // rounded box added to parent
    c2d::RectangleShape *box(c2d::C2DObject *parent, const c2d::FloatRect &rect, const c2d::Color &color,
                             float radius = theme::RADIUS);

    Label *label(c2d::C2DObject *parent, const std::string &text, unsigned size, float x, float y,
                 Weight weight = Weight::Regular, c2d::Color color = theme::text());

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

        Label *caption;
        bool focused = false;
        bool enabled = true;
        bool primary;
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

    // Virtualized vertical list: only the visible rows exist; rows are re-bound when scrolling.
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

        int visibleCount() const { return (int) rows.size(); }

    private:
        void layout();

        Adapter *adapter;
        std::vector<c2d::C2DObject *> rows;
        c2d::RectangleShape *scrollTrack = nullptr;
        c2d::RectangleShape *scrollThumb = nullptr;
        int sel = 0;
        int first = 0;
        bool focus = true;
    };
}

#endif // PS4IPTV_UI_WIDGETS_H
