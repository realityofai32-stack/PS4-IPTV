#include <algorithm>
#include <cmath>

#include "scroll_math.h"

namespace scroll {

    Thumb thumb(float trackLength, int total, int visible, int first, float minLength) {
        Thumb t;
        if (total <= visible || visible <= 0 || trackLength <= 0) {
            return t;
        }
        t.visible = true;
        t.length = std::min(trackLength, std::max(minLength, trackLength * (float) visible / (float) total));
        int maxFirst = total - visible;
        int f = std::min(std::max(first, 0), maxFirst);
        t.offset = (trackLength - t.length) * (float) f / (float) maxFirst;
        return t;
    }

    int firstVisible(int selected, int first, int visible, int count, int margin) {
        if (count <= 0 || visible <= 0) {
            return 0;
        }
        margin = std::max(0, std::min(margin, (visible - 1) / 2));
        if (selected < first + margin) {
            first = selected - margin;
        } else if (selected > first + visible - 1 - margin) {
            first = selected - (visible - 1 - margin);
        }
        return std::max(0, std::min(first, std::max(0, count - visible)));
    }

    int gridMove(int selected, int dx, int dy, int columns, int count) {
        if (count <= 0 || columns <= 0) {
            return -1;
        }
        int col = selected % columns;
        int row = selected / columns;
        int lastRow = (count - 1) / columns;
        if (dx != 0) {
            int c = col + dx;
            if (c < 0 || c >= columns || selected + dx < 0 || selected + dx >= count) {
                return -1;
            }
            return selected + dx;
        }
        if (dy != 0) {
            int r = row + dy;
            if (r < 0 || r > lastRow) {
                return -1;
            }
            return std::min(r * columns + col, count - 1);
        }
        return selected;
    }

    int gridPage(int selected, int pages, int rows, int columns, int count) {
        if (count <= 0 || columns <= 0) {
            return 0;
        }
        int col = selected % columns;
        int lastRow = (count - 1) / columns;
        int row = std::max(0, std::min(lastRow, selected / columns + pages * std::max(1, rows)));
        return std::min(row * columns + col, count - 1);
    }

    int pageTarget(int selected, int pages, int pageSize, int count) {
        if (count <= 0) {
            return 0;
        }
        int target = selected + pages * std::max(1, pageSize);
        return std::max(0, std::min(target, count - 1));
    }

    void Smooth::step(float target, double dt, float rate, float maxLag) {
        if (dt < 0) {
            dt = 0;
        }
        if (dt > 0.1) {
            dt = 0.1;   // after a hitch: a normal step, the lag bound below keeps the focus visible
        }
        float diff = target - pos;
        if (diff > maxLag) {
            pos = target - maxLag;
        } else if (diff < -maxLag) {
            pos = target + maxLag;
        }
        diff = target - pos;
        pos += diff * (float) (1.0 - std::exp(-(double) rate * dt));
        if (std::fabs(target - pos) < 0.008f) {
            pos = target;   // under a pixel away: settled, whole-pixel rows again
        }
    }
}
