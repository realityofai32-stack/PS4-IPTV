// Artwork presentation rules (ui/image_fit.h): Cover / Contain / Poster / Logo placements, the portrait-poster
// backdrop, the quality-limited hero poster and its colour field (v3.1), clipping and degenerate sizes. Synthetic
// dimensions only.

#include <algorithm>
#include <cmath>

#include "check.h"
#include "../../src/ui/image_fit.h"

using namespace fit;

namespace {
    bool near(float a, float b, float tol = 0.01f) {
        return std::fabs(a - b) <= tol;
    }

    // the source and destination have the same aspect ratio: nothing is stretched
    bool undistorted(const Placement &p) {
        return p.valid && near(p.src.w / p.src.h, p.dst.w / p.dst.h, 0.005f * (p.dst.w / p.dst.h));
    }

    // the source stays inside the image and the destination inside the box
    bool inside(const Placement &p, float iw, float ih, float bw, float bh) {
        const float e = 0.01f;
        return p.src.x >= -e && p.src.y >= -e && p.src.x + p.src.w <= iw + e && p.src.y + p.src.h <= ih + e
               && p.dst.x >= -e && p.dst.y >= -e && p.dst.x + p.dst.w <= bw + e && p.dst.y + p.dst.h <= bh + e;
    }
}

TEST(fit_cover_landscape_backdrop) {
    // 16:9 backdrop in the 1320 x 800 hero box (1.65:1): fills it, the sides cropped evenly
    Placement p = place(Mode::Cover, 1024, 576, 1320, 800);
    CHECK(undistorted(p) && inside(p, 1024, 576, 1320, 800));
    CHECK(near(p.dst.x, 0) && near(p.dst.y, 0) && near(p.dst.w, 1320) && near(p.dst.h, 800));
    CHECK(near(p.src.h, 576) && near(p.src.w, 576.0f * 1320 / 800));
    CHECK(near(p.src.x, (1024 - p.src.w) / 2));          // centred by default
    // in a wider box the height is cropped, at anchorY
    Placement band = place(Mode::Cover, 1024, 576, 1920, 800);
    CHECK(undistorted(band) && near(band.src.w, 1024) && near(band.src.h, 1024.0f * 800 / 1920));
    CHECK(near(band.src.y, (576 - band.src.h) / 2));
    Options top;
    top.anchorY = 0;
    CHECK(near(place(Mode::Cover, 1024, 576, 1920, 800, top).src.y, 0));
    // an ultra-wide image in a 16:9 box: the sides are cropped, at anchorX
    Options left;
    left.anchorX = 0;
    Placement w = place(Mode::Cover, 2000, 500, 1600, 900, left);
    CHECK(undistorted(w) && near(w.src.x, 0) && near(w.src.h, 500) && near(w.src.w, 500.0f * 1600 / 900));
    Placement c = place(Mode::Cover, 2000, 500, 1600, 900);
    CHECK(near(c.src.x, (2000 - c.src.w) / 2));
    // anchors outside 0..1 are clamped
    Options wild;
    wild.anchorY = 7;
    Placement a = place(Mode::Cover, 300, 900, 160, 90, wild);
    CHECK(inside(a, 300, 900, 160, 90) && near(a.src.y + a.src.h, 900));
}

TEST(fit_contain_keeps_the_whole_image) {
    Placement p = place(Mode::Contain, 400, 600, 555, 255);   // portrait in a wide box: height-bound
    CHECK(undistorted(p) && inside(p, 400, 600, 555, 255));
    CHECK(near(p.src.x, 0) && near(p.src.y, 0) && near(p.src.w, 400) && near(p.src.h, 600));
    CHECK(near(p.dst.h, 255) && near(p.dst.w, 170));
    CHECK(near(p.dst.x, (555 - 170) / 2.0f) && near(p.dst.y, 0));
    // a small image is enlarged to fit unless an upscale limit is set
    Placement up = place(Mode::Contain, 50, 50, 200, 100);
    CHECK(near(up.dst.h, 100) && near(up.dst.w, 100));
    Options limit;
    limit.maxUpscale = 1.25f;
    Placement lim = place(Mode::Contain, 50, 50, 200, 100, limit);
    CHECK(near(lim.dst.w, 62.5f) && near(lim.dst.x, (200 - 62.5f) / 2) && near(lim.dst.y, (100 - 62.5f) / 2));
}

TEST(fit_portrait_poster) {
    // a 2:3 poster in the 2:3 catalog tile fills it exactly
    Placement same = place(Mode::Poster, 170, 255, 166, 249);
    CHECK(undistorted(same) && inside(same, 170, 255, 166, 249));
    CHECK(near(same.dst.w, 166) && near(same.dst.h, 249));
    // a slightly different poster (27:40) still fills the tile: only a sliver is cropped
    Placement close = place(Mode::Poster, 270, 400, 166, 249);
    CHECK(undistorted(close) && near(close.dst.w, 166) && near(close.dst.h, 249));
    CHECK(close.src.h >= 400 * 0.95f);
    // never forced into a 16:9 crop: a portrait poster in a landscape box is contained, not cropped
    Placement wide = place(Mode::Poster, 400, 600, 408, 230);
    CHECK(undistorted(wide) && inside(wide, 400, 600, 408, 230));
    CHECK(near(wide.src.w, 400) && near(wide.src.h, 600));   // the whole poster
    CHECK(wide.dst.w < 408 * 0.5f && near(wide.dst.h, 230));  // not stretched across the card
    // a landscape still in a portrait tile is shown whole too
    Placement still = place(Mode::Poster, 320, 180, 170, 255);
    CHECK(undistorted(still) && near(still.src.w, 320) && near(still.dst.w, 170));
    // the Home media card: the poster area is the poster's own shape, filled edge to edge
    Placement card = place(Mode::Poster, 170, 255, 170, 255);
    CHECK(undistorted(card) && near(card.dst.x, 0) && near(card.dst.w, 170) && near(card.dst.h, 255));
}

TEST(fit_channel_logo_contain) {
    Options logo;
    logo.padding = 16;
    logo.maxUpscale = 1.6f;
    // a wide logo in the 170 x 255 logo area: whole, centred, inside the padding, never cropped
    Placement p = place(Mode::Logo, 300, 100, 170, 255, logo);
    CHECK(undistorted(p) && inside(p, 300, 100, 170, 255));
    CHECK(near(p.src.w, 300) && near(p.src.h, 100));
    CHECK(near(p.dst.w, 170 - 32) && near(p.dst.x, 16));
    CHECK(near(p.dst.y + p.dst.h / 2, 255 / 2.0f));
    // a tall logo is height-bound by the padding
    Placement tall = place(Mode::Logo, 100, 400, 340, 190, logo);
    CHECK(undistorted(tall) && near(tall.dst.h, 190 - 32) && near(tall.dst.y, 16));
    // a tiny logo is enlarged at most 1.6x (no blurry blow-up)
    Placement tiny = place(Mode::Logo, 40, 40, 340, 190, logo);
    CHECK(near(tiny.dst.w, 64) && near(tiny.dst.h, 64));
    // padding larger than the box: nothing to draw, no negative sizes
    Options huge;
    huge.padding = 200;
    CHECK(!place(Mode::Logo, 100, 100, 340, 190, huge).valid);
}

TEST(fit_portrait_poster_as_backdrop) {
    // the Home hero box 1320 x 800 with a 400 x 600 poster: ratio kept, pushed right, bleeding past the edge
    Placement p = posterBackdrop(400, 600, 1320, 800, 1.1f, 40, 0.15f);
    CHECK(undistorted(p) && inside(p, 400, 600, 1320, 800));
    CHECK(near(p.dst.x + p.dst.w, 1320));                 // reaches the right edge (the bleed is cropped)
    CHECK(near(p.dst.y, 0) && near(p.dst.h, 800));         // the vertical overflow is cropped away
    float drawnW = 800 * 1.1f * 400 / 600;                 // the full poster's width on screen
    CHECK(near(p.dst.x, 1320 + 40 - drawnW));
    CHECK(p.dst.w < 1320 * 0.5f);                          // decorative, never stretched over the hero
    CHECK(near(p.src.y, (600 - 600 / 1.1f) * 0.15f, 0.05f));
    CHECK(p.src.w < 400 && near(p.src.x, 0));              // the right bleed crops the poster's right side
    // a scale below 1 leaves room above and below (placed by the anchor) and still never stretches
    Placement small = posterBackdrop(400, 600, 1360, 1080, 0.8f, 0, 0.5f);
    CHECK(undistorted(small) && near(small.dst.y, (1080 - 864) / 2.0f));
}

namespace {
    // the Home hero's poster fallback (screens/home_screen.cpp heroPosterStyle, layout::home::HERO_POSTER_*)
    PosterArt homeHero() {
        PosterArt a;
        a.height = 800;
        a.maxUpscale = 1.25f;
        a.maxWidth = 560;
        a.minHeight = 400;
        a.rightBleed = 40;
        a.anchorY = 0;
        return a;
    }
}

TEST(fit_hero_poster_never_enlarged_past_the_quality_limit) {
    const float bw = 1320, bh = 800;
    // the hero poster texture (512 x 768): fills the box height at 1.04x, top at the box top, nothing limited
    PosterArtPlacement big = posterArt(512, 768, bw, bh, homeHero());
    CHECK(big.placement.valid && !big.limited && undistorted(big.placement) && inside(big.placement, 512, 768, bw, bh));
    CHECK(near(big.drawn.h, 800) && near(big.drawn.w, 800.0f * 512 / 768) && near(big.scale, 800.0f / 768));
    CHECK(near(big.drawn.y, 0) && near(big.drawn.x + big.drawn.w, bw + 40));   // the bleed is cropped at the edge
    CHECK(near(big.placement.dst.x + big.placement.dst.w, bw) && big.placement.src.w < 512);
    // v3's 400 x 600 texture was drawn 880 tall (1.47x); now 750 (1.25x), still whole and on the right
    PosterArtPlacement mid = posterArt(400, 600, bw, bh, homeHero());
    CHECK(mid.placement.valid && mid.limited && near(mid.scale, 1.25f) && near(mid.drawn.h, 750));
    CHECK(near(mid.drawn.y, 0) && near(mid.placement.dst.h, 750) && near(mid.placement.src.h, 600));
    // a provider poster of 300 x 450 is drawn smaller (562 tall), not blown up
    PosterArtPlacement small = posterArt(300, 450, bw, bh, homeHero());
    CHECK(small.placement.valid && near(small.drawn.h, 562.5f) && near(small.drawn.w, 375));
    // tiny sources are not drawn at all: black space instead of a postage stamp
    CHECK(!posterArt(200, 300, bw, bh, homeHero()).placement.valid);
    // a square image used as a poster stays narrow; a landscape one becomes too short and is not drawn
    PosterArtPlacement square = posterArt(600, 600, bw, bh, homeHero());
    CHECK(square.placement.valid && square.limited && near(square.drawn.w, 560) && near(square.drawn.h, 560));
    CHECK(!posterArt(1280, 720, bw, bh, homeHero()).placement.valid);
    // without limits it is v3's posterBackdrop (detail pages unchanged)
    PosterArt v3;
    v3.height = bh * 1.1f;
    v3.rightBleed = 40;
    v3.anchorY = 0.15f;
    Placement a = posterArt(400, 600, bw, bh, v3).placement;
    Placement b = posterBackdrop(400, 600, bw, bh, 1.1f, 40, 0.15f);
    CHECK(near(a.dst.x, b.dst.x) && near(a.dst.h, b.dst.h) && near(a.src.y, b.src.y) && near(a.src.w, b.src.w));
}

TEST(fit_hero_poster_sweep_keeps_ratio_bounds_and_limit) {
    // every texture size and portrait-ish ratio: never past 1.25x, never distorted, always inside the box and the
    // image, right edge at the bleed, never wider than 560, top at the box top
    const float bw = 1320, bh = 800;
    int drawn = 0;
    for (int h = 60; h <= 2400; h += 37) {
        for (float aspect: {0.5f, 0.6f, 0.667f, 0.7f, 0.75f, 0.9f, 1.0f, 1.3f, 1.78f}) {
            float w = std::round(h * aspect);
            PosterArtPlacement r = posterArt(w, (float) h, bw, bh, homeHero());
            if (!r.placement.valid) {
                CHECK(std::min(800.0f, std::min(h * 1.25f, 560.0f * h / w)) < 400 + 0.01f);
                continue;
            }
            drawn++;
            CHECK(r.scale <= 1.25f + 1e-4f && r.drawn.h <= 800 + 0.01f && r.drawn.w <= 560 + 0.01f);
            CHECK(r.drawn.h >= 400 - 0.01f);
            CHECK(near(r.drawn.w / w, r.drawn.h / h, 1e-4f));          // one scale on both axes
            CHECK(undistorted(r.placement) && inside(r.placement, w, (float) h, bw, bh));
            CHECK(near(r.drawn.x + r.drawn.w, bw + 40) && near(r.drawn.y, 0));
        }
    }
    CHECK(drawn > 100);
}

TEST(fit_ambient_wash_is_a_dim_neutral_field) {
    const Rgb tint = {150, 150, 152}, shade = {5, 6, 8}, page = {5, 6, 8};
    const float shadeA = 150.0f / 255;
    auto luma = [](const Rgb &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
    auto chroma = [](const Rgb &c) {
        return std::max(c.r, std::max(c.g, c.b)) - std::min(c.r, std::min(c.g, c.b));
    };
    // a warm poster (Daryl Dixon's card art averages ~ (76, 57, 35)): a dark warm charcoal, never the page's
    // opposite, darker than the poster as drawn, less saturated than it
    Rgb avg = {76, 57, 35};
    Rgb drawnAvg = {avg.r * tint.r / 255 * (1 - shadeA) + shade.r * shadeA,
                    avg.g * tint.g / 255 * (1 - shadeA) + shade.g * shadeA,
                    avg.b * tint.b / 255 * (1 - shadeA) + shade.b * shadeA};
    Rgb w = ambientWash(avg, tint, shade, shadeA, page, 0.7f, 0.35f, 34);
    CHECK(w.r >= page.r && w.g >= page.g && w.b >= page.b);
    CHECK(luma(w) < luma(drawnAvg) && luma(w) > luma(page));
    CHECK(chroma(w) < chroma(drawnAvg));
    CHECK(std::max(w.r, std::max(w.g, w.b)) <= 34);
    // a white poster: capped, a grey field (no bright slab)
    Rgb white = ambientWash({255, 255, 255}, tint, shade, shadeA, page, 0.7f, 0.35f, 34);
    CHECK(white.r <= 34 && white.g <= 34 && white.b <= 34 && chroma(white) <= 3);
    // a black poster (or level 0): exactly the page - no field, no dark patch
    Rgb black = ambientWash({0, 0, 0}, tint, shade, shadeA, page, 0.7f, 0.35f, 34);
    CHECK(near(black.r, page.r) && near(black.g, page.g) && near(black.b, page.b));
    Rgb off = ambientWash(avg, tint, shade, shadeA, page, 0, 0.35f, 34);
    CHECK(near(off.r, page.r) && near(off.g, page.g) && near(off.b, page.b));
    // a saturated red poster stays a dim, desaturated red-grey
    Rgb red = ambientWash({200, 30, 30}, tint, shade, shadeA, page, 0.7f, 0.35f, 34);
    CHECK(red.r > red.g && red.r <= 34 && chroma(red) < 20);
}

TEST(fit_clip_within_target_rect) {
    Placement p;
    p.valid = true;
    p.src = {0, 0, 100, 200};
    p.dst = {-50, 50, 200, 400};   // 2 box units per texel, overhanging the left and bottom
    Placement c = clip(p, 300, 300);
    CHECK(c.valid && inside(c, 100, 200, 300, 300) && undistorted(c));
    CHECK(near(c.dst.x, 0) && near(c.dst.w, 150) && near(c.dst.y, 50) && near(c.dst.h, 250));
    CHECK(near(c.src.x, 25) && near(c.src.w, 75) && near(c.src.y, 0) && near(c.src.h, 125));
    // entirely outside: nothing to draw
    p.dst = {400, 0, 100, 100};
    CHECK(!clip(p, 300, 300).valid);
    // pixel snapping keeps neighbours seamless
    Rect s = pixelSnap({10.4f, 20.6f, 99.3f, 50.5f});
    CHECK(near(s.x, 10) && near(s.y, 21) && near(s.x + s.w, 110) && near(s.y + s.h, 71));
}

TEST(fit_invalid_dimensions_never_divide_by_zero) {
    const float nan = std::nanf("");
    for (Mode m: {Mode::Cover, Mode::Contain, Mode::Poster, Mode::Logo}) {
        CHECK(!place(m, 0, 100, 100, 100).valid);
        CHECK(!place(m, 100, 0, 100, 100).valid);
        CHECK(!place(m, 100, 100, 0, 100).valid);
        CHECK(!place(m, 100, 100, 100, -5).valid);
        CHECK(!place(m, nan, 100, 100, 100).valid);
        CHECK(!place(m, 100, 100, INFINITY, 100).valid);
        Placement ok = place(m, 1, 1, 1, 1);
        CHECK(ok.valid && std::isfinite(ok.dst.w) && std::isfinite(ok.src.w));
    }
    CHECK(!posterBackdrop(0, 600, 1320, 800, 1.1f, 0, 0.2f).valid);
    CHECK(!posterBackdrop(400, 600, 1320, 0, 1.1f, 0, 0.2f).valid);
    CHECK(!posterBackdrop(400, 600, 1320, 800, 0, 0, 0.2f).valid);
    CHECK(!posterArt(nan, 600, 1320, 800, homeHero()).placement.valid);
    CHECK(!posterArt(400, 600, 1320, 800, PosterArt()).placement.valid);   // no height wanted
    CHECK(!posterArt(400, -1, 1320, 800, homeHero()).placement.valid);
    CHECK(!clip(Placement(), 100, 100).valid);
}
