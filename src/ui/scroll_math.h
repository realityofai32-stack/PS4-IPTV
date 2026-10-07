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
}

#endif // PS4IPTV_UI_SCROLL_MATH_H
