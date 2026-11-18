// V7 Player brand artwork (src/ui/brand_art.*): the monogram's geometry, the colours of every accent, the loading
// screen's composition and animation, and the package images (assets/pic1.png, assets/icon0.png) - their PNG format
// and that they still are what the geometry draws (assets/make_brand.sh regenerates them).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "cross2d/skeleton/stb_image.h"
#include "../../assets/brand/raster.h"
#include "platform/fs.h"
#include "ui/brand_art.h"

namespace {
    float area(const brandart::Point &a, const brandart::Point &b, const brandart::Point &c) {
        return ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2;
    }

    std::string assetPath(const char *name) {
        const char *src = std::getenv("PS4IPTV_SOURCE_DIR");
        return std::string(src ? src : "src") + "/../assets/" + name;
    }

    struct Png {
        bool ok = false;
        unsigned w = 0, h = 0, depth = 0, colorType = 0, interlace = 0;
        size_t bytes = 0;
    };

    // the header fields the PS4 package images depend on (straight from IHDR, no decoder involved)
    Png pngHeader(const std::string &path) {
        Png p;
        std::string data;
        if (!fs::readFile(path, data, 16u * 1024 * 1024)) {
            return p;
        }
        p.bytes = data.size();
        const unsigned char *d = (const unsigned char *) data.data();
        static const unsigned char SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        if (data.size() < 33 || std::memcmp(d, SIG, 8) != 0 || std::memcmp(d + 12, "IHDR", 4) != 0) {
            return p;
        }
        auto be32 = [&](size_t at) { return (unsigned) d[at] << 24 | (unsigned) d[at + 1] << 16 | (unsigned) d[at + 2] << 8 | d[at + 3]; };
        p.w = be32(16);
        p.h = be32(20);
        p.depth = d[24];
        p.colorType = d[25];
        p.interlace = d[28];
        p.ok = true;
        return p;
    }

    // mean absolute difference per channel between a committed PNG and a fresh rasterization of the same mesh
    double meanDifference(const std::string &path, const brandart::Mesh &mesh, int w, int h, palette::Rgba clear) {
        int iw = 0, ih = 0, comp = 0;
        unsigned char *px = stbi_load(path.c_str(), &iw, &ih, &comp, 3);
        if (px == nullptr || iw != w || ih != h) {
            if (px) {
                stbi_image_free(px);
            }
            return 1e9;
        }
        brandraster::Image img = brandraster::render(mesh, w, h, clear, 1, false);
        double sum = 0;
        for (size_t i = 0; i < img.rgb.size(); i++) {
            sum += std::abs((int) img.rgb[i] - (int) px[i]);
        }
        stbi_image_free(px);
        return sum / (double) img.rgb.size();
    }
}

TEST(brand_monogram_geometry) {
    const auto &pieces = brandart::monogram();
    CHECK_EQ(pieces.size(), (size_t) 6);   // V: left arm, point, right arm; 7: bar, stem; the hook
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    float total = 0;
    for (const auto &piece: pieces) {
        CHECK(piece.size() >= 3);
        // a fan from the first point: every triangle turns the same way (no fold-over, no overlap)
        float sign = 0;
        for (size_t i = 1; i + 1 < piece.size(); i++) {
            float a = area(piece[0], piece[i], piece[i + 1]);
            if (sign == 0) {
                sign = a > 0 ? 1.0f : -1.0f;
            }
            CHECK(a * sign > 0);
            total += std::fabs(a);
        }
        for (const auto &p: piece) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }
    // the whole grid is used, nothing outside it
    CHECK(minX == 0 && maxX == brandart::GRID_W && minY == 0 && maxY == brandart::GRID_H);
    // solid strokes, not a hairline: about 40 % of the grid is ink
    CHECK(total > 0.3f * brandart::GRID_W * brandart::GRID_H && total < 0.55f * brandart::GRID_W * brandart::GRID_H);
    // every slanted edge of the strokes runs 0.6 across per unit down (the V's right arm is the 7's twin)
    for (int k: {0, 2, 3, 4}) {
        const auto &p = pieces[(size_t) k];
        float dx = p[2].x - p[1].x, dy = p[2].y - p[1].y;
        CHECK(std::fabs(std::fabs(dx / dy) - 0.6f) < 0.01f);
    }

    // drawn: inside its box at any size, the soft edge included
    brandart::Mesh m;
    const brandart::Colors c = brandart::colorsOf(palette::ACCENTS[0]);
    brandart::addMonogram(m, 10, 20, 30, c, 0.9f);
    CHECK(!m.empty() && m.size() % 3 == 0);
    for (const auto &v: m) {
        CHECK(v.x >= 10 - 1.0f && v.x <= 10 + 30 * brandart::ASPECT + 1.0f && v.y >= 20 - 1.0f && v.y <= 51.0f);
    }
}

TEST(brand_colors_every_accent) {
    for (int i = 0; i < palette::ACCENT_COUNT; i++) {
        const palette::Accent &a = palette::ACCENTS[i];
        const brandart::Colors c = brandart::colorsOf(a);
        // top of the monogram brightest, the accent in the middle, a deeper tone at the bottom
        CHECK(palette::luminance(c.hi) >= palette::luminance(c.mid));
        CHECK(palette::luminance(c.deep) < palette::luminance(c.mid));
        CHECK(c.mid.r == a.accent.r && c.mid.g == a.accent.g && c.mid.b == a.accent.b);
        CHECK(c.hi.a == 255 && c.mid.a == 255 && c.deep.a == 255);
        // the page stays the app's dark background whatever the accent
        CHECK(c.background.r == palette::BACKGROUND.r && c.background.g == palette::BACKGROUND.g
              && c.background.b == palette::BACKGROUND.b);
        // the deep tone keeps the accent's hue family (its strongest channel stays the strongest)
        auto top = [](palette::Rgba x) { return x.r >= x.g && x.r >= x.b ? 0 : x.g >= x.b ? 1 : 2; };
        if (std::string(a.key) != "white") {
            CHECK_EQ(top(c.deep), top(a.accent));
        }
    }
}

TEST(brand_loading_screen_composition) {
    using namespace brandart::splash;
    for (int i = 0; i < palette::ACCENT_COUNT; i++) {
        const brandart::Colors c = brandart::colorsOf(palette::ACCENTS[i]);
        brandart::Mesh logo;
        brandart::splash::logo(logo, c);
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (const auto &v: logo) {
            minX = std::min(minX, v.x);
            maxX = std::max(maxX, v.x);
            minY = std::min(minY, v.y);
            maxY = std::max(maxY, v.y);
        }
        // centred, the size of the reference composition, inside the TV-safe area
        CHECK(std::fabs((minX + maxX) / 2 - W / 2) < 2.0f);
        CHECK(std::fabs((minY + maxY) / 2 - LOGO_CY) < 2.0f);
        CHECK(maxX - minX > W * 0.25f && maxX - minX < W * 0.32f);
        CHECK(minY > 54 && maxY < DOTS_Y - 2 * DOT_R);

        // no ring around the monogram: behind it only soft glows (low alpha, fading to nothing), no stroke
        brandart::Mesh bg;
        background(bg, c);
        CHECK(bg.size() >= 6 && bg[0].c.a == 255);   // the page itself is opaque
        for (size_t k = 6; k < bg.size(); k++) {
            CHECK(bg[k].c.a <= 40);
        }

        brandart::Mesh all;
        frame(all, c, 0.0);
        CHECK(all.size() % 3 == 0);
        for (const auto &v: all) {
            CHECK(std::isfinite(v.x) && std::isfinite(v.y));
        }
        // the dots: on screen, centred, below the monogram
        brandart::Mesh d;
        dots(d, c, 0.0);
        for (const auto &v: d) {
            CHECK(v.y > maxY && v.y < H - 54);
            CHECK(std::fabs(v.x - W / 2) < (DOTS / 2 + 1) * DOT_GAP + 4 * DOT_R);
        }
    }
}

TEST(brand_loading_animation_is_subtle) {
    using namespace brandart::splash;
    // the drift: never more than the amplitude away from where it started, slow (< 1 px per frame at 60 fps)
    for (int w = 0; w < WAVES; w++) {
        for (float x: {100.0f, 700.0f, 1300.0f, 1850.0f}) {
            float y0 = waveY(w, x, 0.0);
            float prev = y0;
            for (int f = 1; f <= 600; f++) {
                float y = waveY(w, x, f / 60.0);
                CHECK(std::fabs(y - y0) <= 2 * WAVE_AMPLITUDE + 0.5f);
                CHECK(std::fabs(y - prev) < 1.0f);
                prev = y;
            }
        }
    }
    // the dots: the middle one is lit on the first frame (the package image), the highlight glides
    CHECK(dotLevel(2, 0.0) > 0.99f);
    for (int i = 0; i < DOTS; i++) {
        if (i != 2) {
            CHECK(dotLevel(i, 0.0) < dotLevel(2, 0.0));
        }
        for (int f = 0; f < 400; f++) {
            float l = dotLevel(i, f / 60.0);
            CHECK(l >= 0.0f && l <= 1.0f);
            CHECK(std::fabs(l - dotLevel(i, (f + 1) / 60.0)) < 0.12f);
        }
    }
    // the same time gives the same picture (frames drawn at uneven intervals stay coherent)
    const brandart::Colors c = brandart::colorsOf(palette::ACCENTS[3]);
    brandart::Mesh a, b;
    frame(a, c, 4.25);
    frame(b, c, 4.25);
    CHECK(a.size() == b.size());
    bool same = a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); i++) {
        same = a[i].x == b[i].x && a[i].y == b[i].y && a[i].c.a == b[i].c.a;
    }
    CHECK(same);
}

TEST(brand_package_images) {
    // PS4 package images: 8-bit RGB (no alpha), not interlaced, the sizes the system expects, small files
    Png pic = pngHeader(assetPath("pic1.png"));
    CHECK(pic.ok);
    CHECK(pic.w == 1920 && pic.h == 1080 && pic.depth == 8 && pic.colorType == 2 && pic.interlace == 0);
    CHECK(pic.bytes > 0 && pic.bytes < 1024 * 1024);
    Png icon = pngHeader(assetPath("icon0.png"));
    CHECK(icon.ok);
    CHECK(icon.w == 512 && icon.h == 512 && icon.depth == 8 && icon.colorType == 2 && icon.interlace == 0);
    CHECK(icon.bytes > 0 && icon.bytes < 256 * 1024);

    // still what the app draws: pic1 is the loading screen's first frame in V7 Blue, icon0 the icon composition
    // (rasterized here without supersampling or dither: only the soft edges differ)
    const brandart::Colors blue = brandart::colorsOf(palette::ACCENTS[palette::DEFAULT_ACCENT]);
    brandart::Mesh splash;
    brandart::splash::frame(splash, blue, 0.0);
    double dPic = meanDifference(assetPath("pic1.png"), splash, 1920, 1080, blue.background);
    std::printf("     pic1.png vs the loading screen's first frame: mean difference %.3f\n", dPic);
    CHECK(dPic < 1.5);
    brandart::Mesh ic;
    brandart::icon::frame(ic, blue);
    double dIcon = meanDifference(assetPath("icon0.png"), ic, 512, 512, blue.background);
    std::printf("     icon0.png vs the icon composition: mean difference %.3f\n", dIcon);
    CHECK(dIcon < 1.5);
}
