// Video geometry (Aspect Ratio, Crop / Fill, Zoom, Position) -> mpv options, what the picture then looks like
// on the 1920x1080 screen (mpv 0.34.1's rectangle math re-implemented below), and the settings defaults /
// migration (English, Auto / Source, no crop, 100 %).

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "check.h"
#include "../../src/player/display.h"
#include "../../src/storage/settings_store.h"

using namespace display;

namespace {
    const int W = 1920;
    const int H = 1080;

    std::map<std::string, std::string> asMap(const std::vector<std::pair<std::string, std::string>> &v) {
        std::map<std::string, std::string> m;
        for (const auto &p: v) {
            m[p.first] = p.second;
        }
        return m;
    }

    double num(const std::map<std::string, std::string> &m, const char *key) {
        auto it = m.find(key);
        return it == m.end() ? NAN : std::atof(it->second.c_str());
    }

    bool near(double a, double b, double eps = 1e-4) {
        return std::fabs(a - b) < eps;
    }

    Geometry geo(Aspect a, Crop c = Crop::None, int zoom = 100, int px = 0, int py = 0) {
        Geometry g;
        g.aspect = a;
        g.crop = c;
        g.zoom = zoom;
        g.posX = px;
        g.posY = py;
        return g;
    }

    // ---- mpv 0.34.1 video/out/aspect.c (mp_get_src_dst_rects), as vo_libmpv calls it: monitor_par 1, no
    // rotation, video-unscaled no, video-scale 1, video-pan 0. Inputs are the options this app sets.
    struct Rect {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

        int w() const { return x1 - x0; }

        int h() const { return y1 - y0; }
    };

    struct Picture {
        Rect src;   // the part of the decoded picture shown
        Rect dst;   // where on the screen
    };

    void panscan(double pan, int dW, int dH, int winW, int winH, int h, int &outW, int &outH) {
        int fwidth = winW;
        int fheight = (int) ((float) winW / dW * dH);
        if (fheight > winH || fheight < h) {
            int tmpw = (int) ((float) winH / dH * dW);
            if (tmpw <= winW) {
                fheight = winH;
                fwidth = tmpw;
            }
        }
        int area = winH - fheight;
        double fw = fwidth / (double) std::max(fheight, 1);
        double fh = 1;
        if (area == 0) {
            area = winW - fwidth;
            fw = 1;
            fh = fheight / (double) std::max(fwidth, 1);
        }
        outW = (int) (fwidth + area * pan * fw);
        outH = (int) (fheight + area * pan * fh);
    }

    void clampSize(int size, int &start, int &end) {
        start = std::max(0, start);
        end = std::min(size, end);
        if (start >= end) {
            start = 0;
            end = 1;
        }
    }

    void split(int srcSize, int dstSize, int scaled, double zoom, double align, int &s0, int &s1, int &d0, int &d1) {
        scaled = (int) (scaled * std::pow(2.0f, (float) zoom));
        double a = (align + 1) / 2;
        s0 = 0;
        s1 = srcSize;
        d0 = (int) ((dstSize - scaled) * a);
        d1 = d0 + scaled;
        int sSrc = s1 - s0;
        int sDst = d1 - d0;
        if (d0 < 0) {
            s0 += -d0 * sSrc / sDst;
            d0 = 0;
        }
        if (d1 > dstSize) {
            s1 -= (d1 - dstSize) * sSrc / sDst;
            d1 = dstSize;
        }
        clampSize(srcSize, s0, s1);
        clampSize(dstSize, d0, d1);
    }

    // srcW x srcH decoded pixels whose display size is dW x dH (before the aspect override)
    Picture show(const Geometry &g, int srcW, int srcH, int dW, int dH) {
        auto o = asMap(mpvOptions(g, W, H));
        // video-aspect-override (f_decoder_wrapper.c): the display size gets the forced ratio
        double ratio = num(o, "video-aspect-override");
        const std::string &a = o["video-aspect-override"];
        size_t colon = a.find(':');
        if (colon != std::string::npos) {
            ratio = std::atof(a.substr(0, colon).c_str()) / std::atof(a.substr(colon + 1).c_str());
        }
        if (ratio > 0) {
            // mp_image_params_set_dsize: pixel aspect so that the display ratio is `ratio`, size kept on one axis
            if (ratio * srcH >= srcW) {
                dW = (int) std::lround(srcH * ratio);
                dH = srcH;
            } else {
                dW = srcW;
                dH = (int) std::lround(srcW / ratio);
            }
        }
        int ml = std::min(std::max((int) (num(o, "video-margin-ratio-left") * W), 0), W);
        int mr = std::min(std::max((int) (num(o, "video-margin-ratio-right") * W), 0), W);
        int mt = std::min(std::max((int) (num(o, "video-margin-ratio-top") * H), 0), H);
        int mb = std::min(std::max((int) (num(o, "video-margin-ratio-bottom") * H), 0), H);
        int vw = W - ml - mr;
        int vh = H - mt - mb;
        int sw, sh;
        panscan(num(o, "panscan"), dW, dH, vw, vh, srcH, sw, sh);
        Picture p;
        split(srcW, vw, sw, num(o, "video-zoom"), num(o, "video-align-x"), p.src.x0, p.src.x1, p.dst.x0, p.dst.x1);
        split(srcH, vh, sh, num(o, "video-zoom"), num(o, "video-align-y"), p.src.y0, p.src.y1, p.dst.y0, p.dst.y1);
        p.dst.x0 += ml;
        p.dst.x1 += ml;
        p.dst.y0 += mt;
        p.dst.y1 += mt;
        return p;
    }

    bool within(int v, int target, int tolerance = 3) {
        return std::abs(v - target) <= tolerance;
    }
}

TEST(geometry_defaults_and_keys) {
    Geometry g;
    CHECK(g.aspect == Aspect::Source && g.crop == Crop::None && g.zoom == 100 && g.posX == 0 && g.posY == 0);
    CHECK(isDefault(defaults()));
    CHECK(!isDefault(geo(Aspect::R4x3)));
    CHECK(!isDefault(geo(Aspect::Source, Crop::Fill)));
    CHECK(!isDefault(geo(Aspect::Source, Crop::None, 110)));
    CHECK(!isDefault(geo(Aspect::Source, Crop::None, 100, 1)));
    // every value has a key that round-trips
    for (int i = 0; i < ASPECT_COUNT; i++) {
        CHECK(aspectFromKey(aspectKey((Aspect) i)) == (Aspect) i);
    }
    for (int i = 0; i < CROP_COUNT; i++) {
        CHECK(cropFromKey(cropKey((Crop) i)) == (Crop) i);
    }
    CHECK(aspectFromKey("bogus") == Aspect::Source);
    CHECK(cropFromKey("") == Crop::None);
    CHECK_EQ(std::string(aspectKey(Aspect::Source)), std::string("source"));
    CHECK_EQ(std::string(cropKey(Crop::Fill)), std::string("fill"));
    // the requested choices, in menu order
    const char *aspects[] = {"source", "16:9", "16:10", "4:3", "5:4", "1:1", "1.85:1", "2.21:1", "2.35:1", "2.39:1",
                             "2.40:1"};
    for (int i = 0; i < ASPECT_COUNT; i++) {
        CHECK_EQ(std::string(aspectKey((Aspect) i)), std::string(aspects[i]));
    }
    const char *crops[] = {"none", "fill", "16:9", "16:10", "4:3", "1:1", "1.85:1", "2.21:1", "2.35:1", "2.39:1",
                           "2.40:1"};
    for (int i = 0; i < CROP_COUNT; i++) {
        CHECK_EQ(std::string(cropKey((Crop) i)), std::string(crops[i]));
    }
    CHECK_EQ(aspectLabel(Aspect::R239), std::string("2.39:1"));
    CHECK_EQ(cropLabel(Crop::R16x10), std::string("16:10"));
    CHECK(aspectLabel(Aspect::Source).empty() && cropLabel(Crop::Fill).empty() && cropLabel(Crop::None).empty());
    // stepping: aspect / crop wrap around, zoom and position stop at the ends
    CHECK(stepAspect(Aspect::Source, -1) == Aspect::R240);
    CHECK(stepAspect(Aspect::R240, 1) == Aspect::Source);
    CHECK(stepAspect(Aspect::Source, 1) == Aspect::R16x9);
    CHECK(stepCrop(Crop::None, 1) == Crop::Fill);
    CHECK(stepCrop(Crop::None, -1) == Crop::R240);
    CHECK_EQ(stepZoom(100, -1), 100);
    CHECK_EQ(stepZoom(100, 1), 105);
    CHECK_EQ(stepZoom(140, 1), 150);
    CHECK_EQ(stepZoom(150, 1), 150);
    CHECK_EQ(stepPosition(POSITION_STEPS, 1), POSITION_STEPS);
    CHECK_EQ(stepPosition(-POSITION_STEPS, -1), -POSITION_STEPS);
    CHECK_EQ(stepPosition(0, 1), 1);
}

TEST(geometry_aspect_maps_to_video_aspect_override) {
    // -1 = the file's own aspect; never "no" (that would force square pixels); integer ratios (no decimals)
    CHECK_EQ(aspectOption(Aspect::Source), std::string("-1"));
    CHECK_EQ(aspectOption(Aspect::R16x9), std::string("16:9"));
    CHECK_EQ(aspectOption(Aspect::R16x10), std::string("16:10"));
    CHECK_EQ(aspectOption(Aspect::R4x3), std::string("4:3"));
    CHECK_EQ(aspectOption(Aspect::R5x4), std::string("5:4"));
    CHECK_EQ(aspectOption(Aspect::R1x1), std::string("1:1"));
    CHECK_EQ(aspectOption(Aspect::R185), std::string("185:100"));
    CHECK_EQ(aspectOption(Aspect::R221), std::string("221:100"));
    CHECK_EQ(aspectOption(Aspect::R235), std::string("235:100"));
    CHECK_EQ(aspectOption(Aspect::R239), std::string("239:100"));
    CHECK_EQ(aspectOption(Aspect::R240), std::string("240:100"));
    for (int i = 1; i < ASPECT_COUNT; i++) {
        // inside mpv's range for video-aspect-override (-1 .. 10)
        double r = aspectRatio((Aspect) i);
        CHECK(r > 0.9 && r < 10);
        auto o = asMap(mpvOptions(geo((Aspect) i), W, H));
        CHECK_EQ(o["video-aspect-override"], aspectOption((Aspect) i));
        // an aspect never crops or zooms by itself
        CHECK_EQ(o["panscan"], std::string("0.0"));
        CHECK(near(num(o, "video-margin-ratio-top"), 0) && near(num(o, "video-margin-ratio-left"), 0));
        CHECK_EQ(o["video-zoom"], std::string("0.000000"));
    }
}

TEST(geometry_option_set_is_complete_and_runtime_only) {
    // every geometry sets the same properties, so a change never leaves a value of the previous one behind
    std::set<std::string> names;
    for (const auto &p: mpvOptions(defaults(), W, H)) {
        names.insert(p.first);
    }
    // only vo / image-parameter properties mpv applies at runtime: no reload, no seek, no track change
    const std::set<std::string> runtime = {"keepaspect", "video-aspect-override", "panscan", "video-margin-ratio-left",
                                           "video-margin-ratio-right", "video-margin-ratio-top",
                                           "video-margin-ratio-bottom", "video-zoom", "video-align-x", "video-align-y",
                                           "video-pan-x", "video-pan-y"};
    CHECK(names == runtime);
    for (int a = 0; a < ASPECT_COUNT; a++) {
        for (int c = 0; c < CROP_COUNT; c++) {
            for (int z: zoomSteps()) {
                auto opts = mpvOptions(geo((Aspect) a, (Crop) c, z, 2, -3), W, H);
                CHECK_EQ(opts.size(), runtime.size());
                std::set<std::string> n;
                for (const auto &p: opts) {
                    n.insert(p.first);
                    CHECK(!p.second.empty());
                }
                CHECK(n == runtime);
                auto o = asMap(opts);
                CHECK_EQ(o["keepaspect"], std::string("yes"));
                // margins stay in mpv's 0..1 range and always leave a visible area
                double lr = num(o, "video-margin-ratio-left") + num(o, "video-margin-ratio-right");
                double tb = num(o, "video-margin-ratio-top") + num(o, "video-margin-ratio-bottom");
                CHECK(lr >= 0 && lr < 0.5 && tb >= 0 && tb < 0.5);
            }
        }
    }
}

TEST(geometry_crop_fill_and_ratio_viewports) {
    auto none = asMap(mpvOptions(geo(Aspect::Source, Crop::None), W, H));
    CHECK_EQ(none["panscan"], std::string("0.0"));
    auto fill = asMap(mpvOptions(geo(Aspect::Source, Crop::Fill), W, H));
    CHECK_EQ(fill["panscan"], std::string("1.0"));
    CHECK(near(num(fill, "video-margin-ratio-top"), 0) && near(num(fill, "video-margin-ratio-left"), 0));
    // a ratio wider than the screen: a centred band of that ratio (bars above and below)
    auto c239 = asMap(mpvOptions(geo(Aspect::Source, Crop::R239), W, H));
    CHECK_EQ(c239["panscan"], std::string("1.0"));
    double band = 1 - num(c239, "video-margin-ratio-top") - num(c239, "video-margin-ratio-bottom");
    CHECK(near(band * H, W / 2.39, 0.5));
    CHECK(near(num(c239, "video-margin-ratio-top"), num(c239, "video-margin-ratio-bottom")));
    CHECK(near(num(c239, "video-margin-ratio-left"), 0));
    // narrower than the screen: pillar bars
    auto c43 = asMap(mpvOptions(geo(Aspect::Source, Crop::R4x3), W, H));
    double cols = 1 - num(c43, "video-margin-ratio-left") - num(c43, "video-margin-ratio-right");
    CHECK(near(cols * W, 1440, 0.5));
    CHECK(near(num(c43, "video-margin-ratio-top"), 0));
    // the screen's own ratio: no margins (= Fill)
    auto c169 = asMap(mpvOptions(geo(Aspect::Source, Crop::R16x9), W, H));
    CHECK(near(num(c169, "video-margin-ratio-top"), 0) && near(num(c169, "video-margin-ratio-left"), 0));
    Margins m = viewport(1.0, W, H);
    CHECK(near(m.left, (1 - 1080.0 / 1920.0) / 2) && near(m.top, 0));
    m = viewport(0, W, H);
    CHECK(m.left == 0 && m.right == 0 && m.top == 0 && m.bottom == 0);
}

TEST(geometry_zoom_and_position_mapping) {
    // the requested steps, translated to mpv's log2 scale
    const std::vector<int> expected = {100, 105, 110, 115, 120, 125, 130, 135, 140, 150};
    CHECK(zoomSteps() == expected);
    for (int z: zoomSteps()) {
        double v = num(asMap(mpvOptions(geo(Aspect::Source, Crop::None, z), W, H)), "video-zoom");
        CHECK(near(std::pow(2.0, v), z / 100.0));
        CHECK(v >= 0 && v < 1);   // well inside mpv's -20 .. 20
    }
    CHECK_EQ(asMap(mpvOptions(geo(Aspect::Source), W, H))["video-zoom"], std::string("0.000000"));
    CHECK_EQ(clampZoom(0), 100);
    CHECK_EQ(clampZoom(111), 110);
    CHECK_EQ(clampZoom(146), 150);
    CHECK_EQ(clampZoom(999), 150);
    // zoom works with every aspect and crop (no Stretch mode any more)
    CHECK(near(std::pow(2.0, num(asMap(mpvOptions(geo(Aspect::R4x3, Crop::R239, 125), W, H)), "video-zoom")), 1.25));
    // position -> video-align (-1 left/top edge .. 1 right/bottom edge), clamped
    auto p = asMap(mpvOptions(geo(Aspect::Source, Crop::None, 130, -POSITION_STEPS, 2), W, H));
    CHECK(near(num(p, "video-align-x"), -1) && near(num(p, "video-align-y"), 0.5));
    p = asMap(mpvOptions(geo(Aspect::Source, Crop::None, 130, 99, -99), W, H));
    CHECK(near(num(p, "video-align-x"), 1) && near(num(p, "video-align-y"), -1));
    CHECK(near(num(p, "video-pan-x"), 0) && near(num(p, "video-pan-y"), 0));
    CHECK_EQ(clampPosition(-7), -POSITION_STEPS);
}

TEST(geometry_reset_restores_defaults) {
    Geometry g = geo(Aspect::R239, Crop::Fill, 150, 3, -2);
    CHECK(!isDefault(g));
    g = defaults();
    CHECK(isDefault(g));
    auto reset = asMap(mpvOptions(g, W, H));
    CHECK_EQ(reset["video-aspect-override"], std::string("-1"));
    CHECK_EQ(reset["panscan"], std::string("0.0"));
    CHECK_EQ(reset["video-zoom"], std::string("0.000000"));
    CHECK(near(num(reset, "video-align-x"), 0) && near(num(reset, "video-align-y"), 0));
    CHECK(near(num(reset, "video-margin-ratio-top"), 0) && near(num(reset, "video-margin-ratio-left"), 0));
}

TEST(geometry_on_screen_results) {
    // 1920x1080 16:9 file
    Picture p = show(defaults(), 1920, 1080, 1920, 1080);
    CHECK(p.dst.w() == 1920 && p.dst.h() == 1080 && p.src.w() == 1920 && p.src.h() == 1080);
    // forced 4:3: the whole picture, squeezed into a centred 1440x1080 (pillarbox), nothing cut off
    p = show(geo(Aspect::R4x3), 1920, 1080, 1920, 1080);
    CHECK(within(p.dst.w(), 1440) && p.dst.h() == 1080 && within(p.dst.x0, 240));
    CHECK(p.src.w() == 1920 && p.src.h() == 1080);
    // forced 2.39:1: the whole picture, letterboxed (stretched horizontally relative to its height)
    p = show(geo(Aspect::R239), 1920, 1080, 1920, 1080);
    CHECK(p.dst.w() == 1920 && within(p.dst.h(), 803) && p.src.h() == 1080);
    // forced 16:10 / 1.85:1 / 2.35:1: whole picture, letterbox height follows the ratio
    CHECK(within(show(geo(Aspect::R16x10), 1920, 1080, 1920, 1080).dst.w(), 1728));
    CHECK(within(show(geo(Aspect::R185), 1920, 1080, 1920, 1080).dst.h(), 1038));
    CHECK(within(show(geo(Aspect::R235), 1920, 1080, 1920, 1080).dst.h(), 817));
    // crop 2.39:1 of a 16:9 file with burned-in bars: only the centre 1920x803 band is shown
    p = show(geo(Aspect::Source, Crop::R239), 1920, 1080, 1920, 1080);
    CHECK(p.dst.w() == 1920 && within(p.dst.h(), 803) && within(p.dst.y0, 138));
    CHECK(within(p.src.h(), 803) && within(p.src.y0, 138) && p.src.w() == 1920);
    // crop 4:3 of a 16:9 file: the centre 1440 columns, pillarboxed
    p = show(geo(Aspect::Source, Crop::R4x3), 1920, 1080, 1920, 1080);
    CHECK(within(p.dst.w(), 1440) && p.dst.h() == 1080 && within(p.src.w(), 1440) && within(p.src.x0, 240));

    // 1280x720 16:9 file: scaled up to the full screen
    p = show(defaults(), 1280, 720, 1280, 720);
    CHECK(p.dst.w() == 1920 && p.dst.h() == 1080 && p.src.w() == 1280);
    p = show(geo(Aspect::Source, Crop::None, 110), 1280, 720, 1280, 720);
    CHECK(p.dst.w() == 1920 && p.dst.h() == 1080 && within(p.src.w(), 1164, 4) && within(p.src.h(), 655, 4));

    // SD 4:3 (720x576 with a 4:3 display size): pillarboxed naturally in Auto
    p = show(defaults(), 720, 576, 768, 576);
    CHECK(within(p.dst.w(), 1440) && p.dst.h() == 1080 && p.src.w() == 720);
    // ... forced 16:9 fills the screen (stretched)
    p = show(geo(Aspect::R16x9), 720, 576, 768, 576);
    CHECK(p.dst.w() == 1920 && p.dst.h() == 1080 && p.src.w() == 720 && p.src.h() == 576);
    // ... Fill Screen: full width, top and bottom cut off, not distorted
    p = show(geo(Aspect::Source, Crop::Fill), 720, 576, 768, 576);
    CHECK(p.dst.w() == 1920 && p.dst.h() == 1080 && p.src.w() == 720 && within(p.src.h(), 432, 2));
    // ... a letterboxed 16:9 movie inside a 4:3 SD picture: crop 16:9 removes the bars and fills the screen
    p = show(geo(Aspect::Source, Crop::R16x9), 720, 576, 768, 576);
    CHECK(p.dst.w() == 1920 && p.dst.h() == 1080 && within(p.src.h(), 432, 2) && within(p.src.y0, 72, 2));

    // cinematic 1920x800 (2.4:1) file: letterboxed in Auto, Fill cuts the sides
    p = show(defaults(), 1920, 800, 1920, 800);
    CHECK(p.dst.w() == 1920 && within(p.dst.h(), 800) && p.src.w() == 1920);
    p = show(geo(Aspect::Source, Crop::Fill), 1920, 800, 1920, 800);
    CHECK(p.dst.w() == 1920 && p.dst.h() == 1080 && within(p.src.w(), 1422, 3) && p.src.h() == 800);
    // ... zoom 125 % on top: a bigger picture, still centred
    p = show(geo(Aspect::Source, Crop::None, 125), 1920, 800, 1920, 800);
    CHECK(p.dst.w() == 1920 && within(p.dst.h(), 1000, 2) && within(p.src.w(), 1536, 2) && within(p.src.x0, 192, 2));
    // ... position: the zoomed picture moved to its left edge / right edge, never beyond
    p = show(geo(Aspect::Source, Crop::None, 125, -POSITION_STEPS), 1920, 800, 1920, 800);
    CHECK(p.src.x0 == 0 && within(p.src.w(), 1536, 2));
    p = show(geo(Aspect::Source, Crop::None, 125, POSITION_STEPS), 1920, 800, 1920, 800);
    CHECK(p.src.x1 == 1920 && within(p.src.w(), 1536, 2));
}

TEST(settings_defaults_and_language_migration) {
    Settings fresh;
    CHECK_EQ(fresh.language, std::string("en"));             // a fresh installation is English
    CHECK(fresh.videoAspect == Aspect::Source);              // never cropped / distorted by default
    CHECK(fresh.videoCrop == Crop::None);
    CHECK_EQ(fresh.zoomPercent, 100);
    CHECK(isDefault(fresh.defaultGeometry()));
    CHECK(fresh.retryDownloads && fresh.resumeDownloadsOnStart);

    SettingsStore s(".");
    std::string err;
    // a settings.json from before the Language setting existed: English, whatever it contained
    CHECK(s.deserialize("{\"version\":1,\"language\":\"tr\",\"resumeVod\":false}", &err));
    CHECK_EQ(s.get().language, std::string("en"));
    CHECK(!s.get().resumeVod);
    CHECK(s.get().videoAspect == Aspect::Source && s.get().videoCrop == Crop::None);
    // no language at all: English
    CHECK(s.deserialize("{\"version\":2}", &err));
    CHECK_EQ(s.get().language, std::string("en"));
    // the user's choice is kept from version 2 on
    s.get().language = "tr";
    s.get().videoAspect = Aspect::R235;
    s.get().videoCrop = Crop::R239;
    s.get().zoomPercent = 115;
    s.get().retryDownloads = false;
    std::string text = s.serialize();
    SettingsStore t(".");
    CHECK(t.deserialize(text, &err));
    CHECK_EQ(t.get().language, std::string("tr"));
    CHECK(t.get().videoAspect == Aspect::R235);
    CHECK(t.get().videoCrop == Crop::R239);
    CHECK_EQ(t.get().zoomPercent, 115);
    CHECK(!t.get().retryDownloads);
    // unknown values fall back safely
    CHECK(t.deserialize("{\"version\":2,\"language\":\"xx\",\"videoAspect\":\"weird\",\"videoCrop\":7,\"zoom\":400}",
                        &err));
    CHECK_EQ(t.get().language, std::string("en"));
    CHECK(t.get().videoAspect == Aspect::Source);
    CHECK(t.get().videoCrop == Crop::None);
    CHECK_EQ(t.get().zoomPercent, 150);
}

TEST(settings_display_mode_migration) {
    // settings.json of the previous version (one combined display mode): same picture as before
    struct Case {
        const char *mode;
        Aspect aspect;
        Crop crop;
    };
    const Case cases[] = {{"auto", Aspect::Source, Crop::None}, {"fit", Aspect::Source, Crop::None},
                          {"fill", Aspect::Source, Crop::Fill}, {"stretch", Aspect::R16x9, Crop::None},
                          {"16:9", Aspect::R16x9, Crop::None}, {"4:3", Aspect::R4x3, Crop::None},
                          {"weird", Aspect::Source, Crop::None}};
    for (const Case &c: cases) {
        SettingsStore s(".");
        std::string err;
        CHECK(s.deserialize(std::string("{\"version\":2,\"displayMode\":\"") + c.mode + "\",\"zoom\":120}", &err));
        CHECK(s.get().videoAspect == c.aspect);
        CHECK(s.get().videoCrop == c.crop);
        CHECK_EQ(s.get().zoomPercent, 120);
    }
    // "stretch" on the 16:9 screen and a forced 16:9 show the same full-screen picture
    Picture p = show(geo(Aspect::R16x9), 720, 576, 768, 576);
    CHECK(p.dst.w() == W && p.dst.h() == H);
    // once saved, the new keys win over a leftover displayMode
    SettingsStore s(".");
    std::string err;
    CHECK(s.deserialize("{\"version\":2,\"displayMode\":\"fill\",\"videoAspect\":\"4:3\",\"videoCrop\":\"none\"}",
                        &err));
    CHECK(s.get().videoAspect == Aspect::R4x3 && s.get().videoCrop == Crop::None);
    CHECK(s.serialize().find("displayMode") == std::string::npos);
}
