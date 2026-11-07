// Recycled grid / list cells always show the right item (ui/scroll_math.h SlotBinder, ui/widgets.cpp GridView /
// ListView place()). Hardware bug (v7-ux-7, Movies and Series): scroll down several rows, press Up once - the top two
// visible rows sometimes showed the same items until the next key press. Root cause: cells were (re)bound only on the
// frame floor(scroll position) changed, and only the cells visible on that frame. Gliding up, the top slot is still
// clipped above the grid on that frame when the step is small (a fast logic frame); a frame later it slides into view
// still bound to the row it showed before - the row now drawn below it. This test runs the grid frame by frame with
// the real easing and checks every shown cell on every frame.

#include <algorithm>
#include <cmath>
#include <vector>

#include "check.h"
#include "../../src/ui/layout.h"
#include "../../src/ui/scroll_math.h"

namespace {
    // GridView's slot logic without libcross2d (same order: layout() on a move, place() every frame while gliding)
    struct SimGrid {
        int cols, rows, peek, count;
        float pitch, cellH, extent;
        bool oldRule;                      // the pre-fix rule: bind only when floor(pos) changes
        scroll::Smooth smooth;
        scroll::SlotBinder binder;
        std::vector<int> shows;            // what each slot displays (its last bound item)
        int firstRow = 0, sel = 0, boundBase = -1;
        int staleFrames = 0;

        SimGrid(int c, int r, int p, int n, float pitchY, float cellHeight, float ext, bool old)
                : cols(c), rows(r), peek(p), count(n), pitch(pitchY), cellH(cellHeight), extent(ext), oldRule(old) {
            shows.assign((size_t) (cols * (rows + peek + 1)), -1);
            binder.resize((int) shows.size());
            layout(false);
        }

        int totalRows() const { return (count - 1) / cols + 1; }

        void layout(bool animate) {
            firstRow = scroll::firstVisible(sel / cols, firstRow, rows, totalRows(), rows >= 3 ? 1 : 0);
            if (!animate) {
                smooth.snap((float) firstRow);
            } else {
                smooth.pos = std::min(std::max(smooth.pos, 0.0f), (float) std::max(0, totalRows() - rows));
            }
            binder.invalidate();
            boundBase = -1;
            place();
        }

        void place() {
            int base = (int) std::floor(smooth.pos);
            float frac = smooth.pos - (float) base;
            bool rebind = base != boundBase;
            boundBase = base;
            const int drawn = rows + peek;
            const float drawnExtent = extent + (float) peek * pitch;
            for (int r = 0; r <= drawn; r++) {
                bool shown = scroll::slotShown(r, drawn, frac, pitch, cellH, drawnExtent);
                for (int c = 0; c < cols; c++) {
                    int slot = r * cols + c;
                    int index = (base + r) * cols + c;
                    if (!shown || index >= count) {
                        continue;
                    }
                    bool bind = oldRule ? rebind : binder.needsBind(slot, index);
                    if (bind) {
                        shows[(size_t) slot] = index;
                    }
                    if (shows[(size_t) slot] != index) {
                        staleFrames++;   // a visible cell showing another item: the hardware bug
                        return;
                    }
                }
            }
        }

        void press(int dy) {
            int target = scroll::gridMove(sel, 0, dy, cols, count);
            if (target >= 0 && target != sel) {
                sel = target;
                layout(true);
            }
        }

        void frames(int n, double dt) {
            for (int i = 0; i < n; i++) {
                if (!smooth.settled((float) firstRow)) {
                    smooth.step((float) firstRow, dt);
                    place();
                }
            }
        }
    };

    // Movies / Series grid (layout::catalog): 2 whole rows + 1 peek row, cells 325 px, 30 px row gap
    SimGrid browseGrid(bool oldRule) {
        namespace B = layout::catalog;
        const float pitch = B::CELL_H + B::ROW_GAP;
        return SimGrid(B::COLUMNS, B::ROWS, B::PEEK_ROWS, 400, pitch, B::CELL_H, B::GRID_H, oldRule);
    }
}

TEST(grid_regression_scroll_down_then_up_once) {
    // the exact hardware sequence at several logic frame lengths (fast idle loop .. a slow frame)
    for (double dt: {0.002, 0.004, 0.008, 1.0 / 60, 1.0 / 30, 0.1}) {
        SimGrid g = browseGrid(false);
        for (int i = 0; i < 6; i++) {     // down several rows, each glide settling
            g.press(1);
            g.frames(120, dt);
        }
        CHECK(g.firstRow > 0);
        g.press(-1);                      // Up once
        g.frames(240, dt);
        CHECK_EQ(g.staleFrames, 0);
        // focus and scroll position are where they should be
        CHECK(g.sel / g.cols >= g.firstRow && g.sel / g.cols < g.firstRow + g.rows);
        CHECK(g.smooth.settled((float) g.firstRow));
    }
    // the pre-fix rule reproduces the bug in the same sequence on a fast frame (the top slot slides in unbound)
    SimGrid old = browseGrid(true);
    for (int i = 0; i < 6; i++) {
        old.press(1);
        old.frames(120, 0.004);
    }
    old.press(-1);
    old.frames(240, 0.004);
    CHECK(old.staleFrames > 0);
}

TEST(grid_regression_rapid_up_down) {
    // held / rapid presses: a new move every 1..5 frames, both directions, never a stale visible cell
    for (double dt: {0.004, 1.0 / 60, 1.0 / 30}) {
        for (int every: {1, 2, 3, 5}) {
            SimGrid g = browseGrid(false);
            for (int i = 0; i < 25; i++) {
                g.press(1);
                g.frames(every, dt);
            }
            for (int i = 0; i < 18; i++) {
                g.press(-1);
                g.frames(every, dt);
            }
            for (int i = 0; i < 12; i++) {
                g.press(i % 2 ? 1 : -1);     // alternating Up / Down
                g.frames(every, dt);
            }
            g.frames(240, dt);
            CHECK_EQ(g.staleFrames, 0);
            CHECK(g.sel / g.cols >= g.firstRow && g.sel / g.cols < g.firstRow + g.rows);
        }
    }
}

TEST(list_regression_scroll_down_then_up_once) {
    // ListView uses the same slot rule (rows of 96 px, 4 px apart, 8 visible): the same sequence
    for (double dt: {0.002, 0.004, 1.0 / 60}) {
        SimGrid l(1, 8, 0, 300, 100, 96, 8 * 96 + 7 * 4, false);
        for (int i = 0; i < 20; i++) {
            l.press(1);
            l.frames(60, dt);
        }
        l.press(-1);
        l.frames(240, dt);
        for (int i = 0; i < 30; i++) {
            l.press(i % 3 ? -1 : 1);
            l.frames(2, dt);
        }
        l.frames(240, dt);
        CHECK_EQ(l.staleFrames, 0);
    }
}

TEST(slot_binder_basics) {
    scroll::SlotBinder b;
    b.resize(3);
    CHECK(b.boundIndex(0) == -1 && b.needsBind(0, 5) && !b.needsBind(0, 5) && b.boundIndex(0) == 5);
    CHECK(b.needsBind(0, 6));                     // shown for another item: bound again
    b.invalidate();                               // data / selection / focus changed
    CHECK(b.needsBind(0, 6) && b.needsBind(2, 9) && b.needsBind(-1, 1) && b.needsBind(7, 1));
    // the top slot clipped / shown: browse grid pitch 355, cells 325 (shown while frac < 325 / 355 = 0.915)
    CHECK(scroll::slotShown(0, 3, 0.9f, 355, 325, 1035) && !scroll::slotShown(0, 3, 0.93f, 355, 325, 1035));
    CHECK(!scroll::slotShown(3, 3, 0, 355, 325, 1035) && scroll::slotShown(3, 3, 0.2f, 355, 325, 1035));
}
