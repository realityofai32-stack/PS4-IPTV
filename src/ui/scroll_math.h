// List scrolling and scrollbar geometry (host-testable).

#ifndef PS4IPTV_UI_SCROLL_MATH_H
#define PS4IPTV_UI_SCROLL_MATH_H

#include <vector>

namespace scroll {

    struct Thumb {
        bool visible = false;   // false when everything fits (no scrollbar)
        float offset = 0;       // from the top of the track
        float length = 0;
    };

    // thumb length proportional to visible/total, position proportional to first/(total-visible)
    Thumb thumb(float trackLength, int total, int visible, int first, float minLength);

    // first visible row that keeps `selected` on screen with `margin` rows of context while scrolling
    int firstVisible(int selected, int first, int visible, int count, int margin);

    // target of a page jump of `pages` pages (negative = up), clamped to the list
    int pageTarget(int selected, int pages, int pageSize, int count);

    // Grid navigation (row-major, `columns` per row). Returns the new index, or -1 when the move hits an
    // edge (Left in the first column, Up in the first row...): the caller may move focus elsewhere.
    // Down into a shorter last row lands on its last item.
    int gridMove(int selected, int dx, int dy, int columns, int count);

    // page jump of `pages` screens of `rows` rows, keeping the column where possible
    int gridPage(int selected, int pages, int rows, int columns, int count);

    // Viewport easing for lists and grids. The logical first row (where focus says the view must be) changes
    // at once; the drawn position `pos` (fractional rows) follows it frame-rate independently (exponential
    // approach: the remaining distance shrinks by exp(-rate * dt) per frame), never overshoots and never lags
    // more than maxLag rows (so the focused row stays on screen at full repeat speed). Input never waits.
    struct Smooth {
        static constexpr float RATE = 20;       // per second: ~90 % of a step is covered in 0.12 s
        static constexpr float MAX_LAG = 1;     // rows

        float pos = 0;

        // advances pos toward target; dt is clamped (a slow frame never jumps past the target)
        void step(float target, double dt, float rate = RATE, float maxLag = MAX_LAG);

        void snap(float target) { pos = target; }

        bool settled(float target) const { return pos == target; }
    };

    // Which item each recycled cell slot of a list / grid shows (v7-ux-8 fix). Slots are drawn at the eased position:
    // slot r shows row floor(pos) + r. A slot is bound when it is SHOWN for an item it was not bound to - not only
    // when floor(pos) changes: while gliding up, the top slot can still be clipped (hidden) on the frame floor(pos)
    // changes and become visible a frame later; binding only on that frame left it showing the row below
    // (duplicated / stale top rows in Movies / Series until the next key press).
    class SlotBinder {
    public:
        void resize(int slots);

        // data, selection or focus changed: every slot binds again when it is shown
        void invalidate();

        // slot `slot` is shown for item `index` this frame: true = bind it now (remembered)
        bool needsBind(int slot, int index);

        // the item slot `slot` was last bound to (-1: none)
        int boundIndex(int slot) const;

    private:
        std::vector<int> bound;
    };

    // whether slot r (0 .. drawn, drawn = the extra slot that enters while gliding) is on screen at the fractional
    // offset frac (0 <= frac < 1): its top at (r - frac) * pitch, a part of it inside 0 .. shownExtent
    bool slotShown(int r, int drawn, float frac, float pitch, float cellHeight, float shownExtent);
}

#endif // PS4IPTV_UI_SCROLL_MATH_H
