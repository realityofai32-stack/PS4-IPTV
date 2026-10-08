#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "display.h"

namespace display {

    const std::vector<int> &zoomSteps() {
        static const std::vector<int> steps = {100, 105, 110, 115, 120, 125};
        return steps;
    }

    const char *modeKey(Mode m) {
        switch (m) {
            case Mode::Fit:
                return "fit";
            case Mode::Fill:
                return "fill";
            case Mode::Stretch:
                return "stretch";
            case Mode::Aspect16x9:
                return "16:9";
            case Mode::Aspect4x3:
                return "4:3";
            default:
                return "auto";
        }
    }

    Mode modeFromKey(const std::string &key) {
        for (int i = 0; i < MODE_COUNT; i++) {
            if (key == modeKey((Mode) i)) {
                return (Mode) i;
            }
        }
        return Mode::Auto;
    }

    const char *modeNameKey(Mode m) {
        switch (m) {
            case Mode::Fit:
                return "display.mode.fit";
            case Mode::Fill:
                return "display.mode.fill";
            case Mode::Stretch:
                return "display.mode.stretch";
            case Mode::Aspect16x9:
                return "display.mode.16x9";
            case Mode::Aspect4x3:
                return "display.mode.4x3";
            default:
                return "display.mode.auto";
        }
    }

    const char *modeDescKey(Mode m) {
        switch (m) {
            case Mode::Fit:
                return "display.mode.fit_desc";
            case Mode::Fill:
                return "display.mode.fill_desc";
            case Mode::Stretch:
                return "display.mode.stretch_desc";
            case Mode::Aspect16x9:
                return "display.mode.16x9_desc";
            case Mode::Aspect4x3:
                return "display.mode.4x3_desc";
            default:
                return "display.mode.auto_desc";
        }
    }

    int clampZoom(int percent) {
        int best = 100;
        int bestDiff = 1 << 30;
        for (int s: zoomSteps()) {
            int d = std::abs(s - percent);
            if (d < bestDiff) {
                best = s;
                bestDiff = d;
            }
        }
        return best;
    }

    bool zoomAvailable(Mode m) {
        return m != Mode::Stretch;
    }

    std::vector<std::pair<std::string, std::string>> mpvOptions(Mode m, int zoomPercent) {
        int z = clampZoom(zoomPercent);
        char zoom[32];
        // log2 scale; exactly "0.000000" for 100 %
        snprintf(zoom, sizeof(zoom), "%.6f", z == 100 || !zoomAvailable(m) ? 0.0 : std::log2((double) z / 100.0));
        const char *aspect = m == Mode::Aspect16x9 ? "16:9" : m == Mode::Aspect4x3 ? "4:3" : "-1";
        return {
                {"keepaspect", m == Mode::Stretch ? "no" : "yes"},
                {"panscan", m == Mode::Fill ? "1.0" : "0.0"},
                {"video-aspect-override", aspect},
                {"video-zoom", zoom},
        };
    }
}
