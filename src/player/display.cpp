#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "display.h"

namespace display {

    namespace {
        // per aspect / crop ratio: settings key, label, mpv ratio (integers: no decimal parsing in mpv), value
        struct Ratio {
            const char *key;
            const char *label;
            const char *mpv;
            double value;
        };

        const Ratio RATIOS[] = {
                {"16:9",   "16:9",   "16:9",    16.0 / 9.0},
                {"16:10",  "16:10",  "16:10",   16.0 / 10.0},
                {"4:3",    "4:3",    "4:3",     4.0 / 3.0},
                {"5:4",    "5:4",    "5:4",     5.0 / 4.0},
                {"1:1",    "1:1",    "1:1",     1.0},
                {"1.85:1", "1.85:1", "185:100", 1.85},
                {"2.21:1", "2.21:1", "221:100", 2.21},
                {"2.35:1", "2.35:1", "235:100", 2.35},
                {"2.39:1", "2.39:1", "239:100", 2.39},
                {"2.40:1", "2.40:1", "240:100", 2.40},
        };

        // index into RATIOS (-1 = none)
        int aspectRatioIndex(Aspect a) {
            switch (a) {
                case Aspect::R16x9:
                    return 0;
                case Aspect::R16x10:
                    return 1;
                case Aspect::R4x3:
                    return 2;
                case Aspect::R5x4:
                    return 3;
                case Aspect::R1x1:
                    return 4;
                case Aspect::R185:
                    return 5;
                case Aspect::R221:
                    return 6;
                case Aspect::R235:
                    return 7;
                case Aspect::R239:
                    return 8;
                case Aspect::R240:
                    return 9;
                default:
                    return -1;
            }
        }

        int cropRatioIndex(Crop c) {
            switch (c) {
                case Crop::R16x9:
                    return 0;
                case Crop::R16x10:
                    return 1;
                case Crop::R4x3:
                    return 2;
                case Crop::R1x1:
                    return 4;
                case Crop::R185:
                    return 5;
                case Crop::R221:
                    return 6;
                case Crop::R235:
                    return 7;
                case Crop::R239:
                    return 8;
                case Crop::R240:
                    return 9;
                default:
                    return -1;
            }
        }

        std::string number(double v) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.6f", v);
            return buf;
        }
    }

    Geometry defaults() {
        return Geometry();
    }

    bool isDefault(const Geometry &g) {
        return g == Geometry();
    }

    const std::vector<int> &zoomSteps() {
        static const std::vector<int> steps = {100, 105, 110, 115, 120, 125, 130, 135, 140, 150};
        return steps;
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

    int clampPosition(int step) {
        return step < -POSITION_STEPS ? -POSITION_STEPS : step > POSITION_STEPS ? POSITION_STEPS : step;
    }

    Aspect stepAspect(Aspect a, int delta) {
        int i = ((int) a + (delta < 0 ? ASPECT_COUNT - 1 : 1)) % ASPECT_COUNT;
        return (Aspect) i;
    }

    Crop stepCrop(Crop c, int delta) {
        int i = ((int) c + (delta < 0 ? CROP_COUNT - 1 : 1)) % CROP_COUNT;
        return (Crop) i;
    }

    int stepZoom(int percent, int delta) {
        const auto &steps = zoomSteps();
        int at = 0;
        int z = clampZoom(percent);
        for (size_t k = 0; k < steps.size(); k++) {
            if (steps[k] == z) {
                at = (int) k;
            }
        }
        at += delta < 0 ? -1 : 1;
        at = at < 0 ? 0 : at >= (int) steps.size() ? (int) steps.size() - 1 : at;
        return steps[(size_t) at];
    }

    int stepPosition(int step, int delta) {
        return clampPosition(step + (delta < 0 ? -1 : 1));
    }

    const char *aspectKey(Aspect a) {
        int i = aspectRatioIndex(a);
        return i < 0 ? "source" : RATIOS[i].key;
    }

    Aspect aspectFromKey(const std::string &key) {
        for (int i = 0; i < ASPECT_COUNT; i++) {
            if (key == aspectKey((Aspect) i)) {
                return (Aspect) i;
            }
        }
        return Aspect::Source;
    }

    const char *cropKey(Crop c) {
        if (c == Crop::None) {
            return "none";
        }
        if (c == Crop::Fill) {
            return "fill";
        }
        return RATIOS[cropRatioIndex(c)].key;
    }

    Crop cropFromKey(const std::string &key) {
        for (int i = 0; i < CROP_COUNT; i++) {
            if (key == cropKey((Crop) i)) {
                return (Crop) i;
            }
        }
        return Crop::None;
    }

    double aspectRatio(Aspect a) {
        int i = aspectRatioIndex(a);
        return i < 0 ? 0 : RATIOS[i].value;
    }

    double cropRatio(Crop c) {
        int i = cropRatioIndex(c);
        return i < 0 ? 0 : RATIOS[i].value;
    }

    std::string aspectLabel(Aspect a) {
        int i = aspectRatioIndex(a);
        return i < 0 ? "" : RATIOS[i].label;
    }

    std::string cropLabel(Crop c) {
        int i = cropRatioIndex(c);
        return i < 0 ? "" : RATIOS[i].label;
    }

    std::string aspectOption(Aspect a) {
        int i = aspectRatioIndex(a);
        return i < 0 ? "-1" : RATIOS[i].mpv;
    }

    void migrateDisplayMode(const std::string &mode, Aspect &aspect, Crop &crop) {
        aspect = Aspect::Source;
        crop = Crop::None;
        if (mode == "fill") {
            crop = Crop::Fill;
        } else if (mode == "stretch" || mode == "16:9") {
            aspect = Aspect::R16x9;
        } else if (mode == "4:3") {
            aspect = Aspect::R4x3;
        }
    }

    Margins viewport(double ratio, int windowW, int windowH) {
        Margins m;
        if (ratio <= 0 || windowW <= 0 || windowH <= 0) {
            return m;
        }
        double screen = (double) windowW / (double) windowH;
        if (std::fabs(ratio - screen) < 1e-6) {
            return m;
        }
        if (ratio > screen) {
            // wider than the screen: full width, bars above and below
            m.top = m.bottom = (1.0 - screen / ratio) / 2.0;
        } else {
            m.left = m.right = (1.0 - ratio / screen) / 2.0;
        }
        return m;
    }

    std::vector<std::pair<std::string, std::string>> mpvOptions(const Geometry &g, int windowW, int windowH) {
        int z = clampZoom(g.zoom);
        Margins m = viewport(cropRatio(g.crop), windowW, windowH);
        bool cropping = g.crop != Crop::None;
        return {
                {"keepaspect",               "yes"},
                {"video-aspect-override",    aspectOption(g.aspect)},
                {"panscan",                  cropping ? "1.0" : "0.0"},
                {"video-margin-ratio-left",  number(m.left)},
                {"video-margin-ratio-right", number(m.right)},
                {"video-margin-ratio-top",   number(m.top)},
                {"video-margin-ratio-bottom", number(m.bottom)},
                // log2 scale; exactly "0.000000" for 100 %
                {"video-zoom",               number(z == 100 ? 0.0 : std::log2((double) z / 100.0))},
                {"video-align-x",            number((double) clampPosition(g.posX) / POSITION_STEPS)},
                {"video-align-y",            number((double) clampPosition(g.posY) / POSITION_STEPS)},
                {"video-pan-x",              "0.000000"},
                {"video-pan-y",              "0.000000"},
        };
    }
}
