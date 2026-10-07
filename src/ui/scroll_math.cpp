#include <algorithm>

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

    int pageTarget(int selected, int pages, int pageSize, int count) {
        if (count <= 0) {
            return 0;
        }
        int target = selected + pages * std::max(1, pageSize);
        return std::max(0, std::min(target, count - 1));
    }
}
