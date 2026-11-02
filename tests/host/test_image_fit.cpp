// Artwork presentation rules (ui/image_fit.h): Cover / Contain / Poster / Logo placements, the portrait-poster
// backdrop, clipping and degenerate sizes. Synthetic dimensions only.

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
    CHECK(!clip(Placement(), 100, 100).valid);
}
