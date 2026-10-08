// List scrolling and scrollbar geometry (host-testable).

#ifndef PS4IPTV_UI_SCROLL_MATH_H
#define PS4IPTV_UI_SCROLL_MATH_H

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
}

#endif // PS4IPTV_UI_SCROLL_MATH_H
