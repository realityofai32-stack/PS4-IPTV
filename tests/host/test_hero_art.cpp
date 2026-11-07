// The Home hero's artwork (images/hero_art.h): a usable provider backdrop full-bleed; else the SHOWCASE - the best
// provider poster (the detail cover when it differs from the list image) as a sharp framed object, never enlarged,
// on an ambient field of its own colour; else the clean dark hero. No generated, blurred or cropped artwork.
// Also simulated with an asynchronous loader: cache hit / miss, rapid hero changes, a stale result, repeated visits.

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <string>

#include "check.h"
#include "../../src/images/hero_art.h"
#include "../../src/ui/layout.h"

using images::hero::Inputs;
using images::hero::Plan;
using images::hero::Show;

TEST(hero_art_provider_backdrop_usability) {
    CHECK(images::hero::usableBackdrop(1280, 720) && images::hero::usableBackdrop(1920, 1080));
    CHECK(images::hero::usableBackdrop(780, 439));
    CHECK(!images::hero::usableBackdrop(600, 900));     // a portrait poster sent as backdrop_path
    CHECK(!images::hero::usableBackdrop(500, 281));     // a thumbnail
    CHECK(!images::hero::usableBackdrop(800, 700));     // nearly square
    CHECK(!images::hero::usableBackdrop(0, 0));
}

TEST(hero_art_priority_plan) {
    // 1. a valid provider backdrop: full-bleed, no poster requested (one large texture)
    Inputs good;
    good.backdropKnown = good.backdropLoaded = good.posterKnown = good.coverKnown = true;
    good.backdropW = 1280;
    good.backdropH = 720;
    Plan p = images::hero::plan(good);
    CHECK(p.show == Show::Backdrop && p.backdropUsable && p.wantBackdrop && !p.wantPoster && !p.wantCover);
    good.posterLoaded = good.coverLoaded = true;           // even with posters still in memory
    CHECK(images::hero::plan(good).show == Show::Backdrop);
    // 2. no backdrop, a detail cover (cover_big / movie_image) that differs from the list image: the cover only
    Inputs cover;
    cover.posterKnown = cover.coverKnown = true;
    p = images::hero::plan(cover);
    CHECK(p.wantCover && !p.wantPoster && !p.wantBackdrop && p.show == Show::Nothing);
    cover.posterLoaded = true;                             // the list image already in memory: shown meanwhile
    CHECK(images::hero::plan(cover).show == Show::Poster);
    cover.coverLoaded = true;
    CHECK(images::hero::plan(cover).show == Show::Cover);  // the best image wins once it is there
    // 3. the detail cover fails: the list image instead
    Inputs failed = cover;
    failed.coverLoaded = false;
    failed.coverFailed = true;
    p = images::hero::plan(failed);
    CHECK(!p.wantCover && p.wantPoster && p.show == Show::Poster);
    // 4. no detail cover (details pending, or the same URL as the list): the list image
    Inputs list;
    list.posterKnown = true;
    p = images::hero::plan(list);
    CHECK(p.wantPoster && !p.wantCover && p.show == Show::Nothing);
    list.posterLoaded = true;
    CHECK(images::hero::plan(list).show == Show::Poster);
    // 5. a backdrop that is loading: only the backdrop is requested; a poster in memory stays meanwhile
    Inputs loading;
    loading.backdropKnown = loading.posterKnown = loading.coverKnown = true;
    p = images::hero::plan(loading);
    CHECK(p.wantBackdrop && !p.wantPoster && !p.wantCover && p.show == Show::Nothing);
    loading.posterLoaded = true;
    CHECK(images::hero::plan(loading).show == Show::Poster);
    // 6. a backdrop that failed or turned out unusable (portrait / thumbnail): the showcase
    Inputs bad;
    bad.backdropKnown = bad.backdropLoaded = bad.coverKnown = bad.coverLoaded = true;
    bad.backdropW = 600;
    bad.backdropH = 900;
    p = images::hero::plan(bad);
    CHECK(!p.backdropUsable && !p.wantBackdrop && p.wantCover && p.show == Show::Cover);
    Inputs gone;
    gone.backdropKnown = gone.backdropFailed = gone.posterKnown = true;
    CHECK(images::hero::plan(gone).wantPoster);
    // 7. no artwork at all, or every image failed: the clean dark hero, nothing requested
    p = images::hero::plan(Inputs());
    CHECK(p.show == Show::Nothing && !p.wantBackdrop && !p.wantPoster && !p.wantCover);
    Inputs allFailed;
    allFailed.posterKnown = allFailed.posterFailed = allFailed.coverKnown = allFailed.coverFailed = true;
    p = images::hero::plan(allFailed);
    CHECK(p.show == Show::Nothing && !p.wantPoster && !p.wantCover);
}

TEST(hero_art_showcase_poster_size_and_place) {
    namespace H = images::hero;
    CHECK(H::SHOWCASE_H == 560 && H::SHOWCASE_MIN_H == 300 && H::SHOWCASE_MAX_W == 420 && H::SHOWCASE_RIGHT == 1680);
    // the HeroPoster texture of a 600 x 900 source (512 x 768): shrunk to 560 tall - sharp, never enlarged
    H::Showcase s = H::showcase(512, 768);
    CHECK(s.shown && s.h == 560 && s.w == 373 && s.scale < 1);
    CHECK(s.x + s.w == H::SHOWCASE_RIGHT && std::fabs(s.y + s.h / 2 - H::SHOWCASE_CENTER_Y) <= 1);
    // a 300 x 450 source: drawn at its own size (1.0x), not enlarged to 560
    s = H::showcase(300, 450);
    CHECK(s.shown && s.w == 300 && s.h == 450 && s.scale == 1);
    // exactly the minimum, and just under it: omitted (the clean dark hero)
    CHECK(H::showcase(200, 300).shown && H::showcase(200, 300).h == 300);
    CHECK(!H::showcase(199, 299).shown && !H::showcase(0, 0).shown && !H::showcase(-5, 900).shown);
    // square / wide covers: at most 420 wide, ratio kept
    s = H::showcase(512, 512);
    CHECK(s.shown && s.w == 420 && s.h == 420);
    s = H::showcase(512, 340);
    CHECK(s.shown && s.w == 420 && std::fabs(s.h - 279) <= 1);
    // every size: ratio kept within a pixel, never enlarged, inside the hero (right of the text, above the rails)
    for (int w = 100; w <= 512; w += 37) {
        for (int h = 300; h <= 768; h += 41) {
            H::Showcase t = H::showcase(w, h);
            CHECK(t.shown && t.scale <= 1.0f + 1e-6f && t.w <= H::SHOWCASE_MAX_W && t.h <= H::SHOWCASE_H);
            CHECK(std::fabs(t.w / t.h - (float) w / (float) h) < 0.02f);
            CHECK(t.x >= layout::home::HERO_TEXT_RIGHT + 100 && t.y >= 100 && t.y + t.h <= layout::home::RAILS_Y);
        }
    }
}

TEST(hero_art_ambient_field_and_levels) {
    namespace H = images::hero;
    const H::Rgb page = {5, 6, 8};
    // v7-ux-7: subtle - a dark shade of the poster's colour, never a bright / grey field
    CHECK(H::AMBIENT_MAX <= 36 && H::AMBIENT_LEVEL <= 0.2f && H::AMBIENT_DESATURATE <= 0.2f);
    H::Rgb red = H::ambient({220, 40, 30}, page);
    CHECK(red.r > red.g && red.r > red.b && red.r <= H::AMBIENT_MAX && red.g >= page.g && red.b >= page.b);
    // the hue is kept (not washed to grey): a red poster gives a clearly red shade, a blue one a blue shade
    CHECK(red.r - std::max(red.g, red.b) >= 15);
    H::Rgb blue = H::ambient({30, 60, 210}, page);
    CHECK(blue.b > blue.r && blue.b > blue.g && blue.b - std::max(blue.r, blue.g) >= 10);
    // each title stays distinct
    H::Rgb green = H::ambient({40, 180, 60}, page);
    CHECK(green.g > green.r && green.g > green.b);
    CHECK(!(red.r == blue.r && red.g == blue.g && red.b == blue.b));
    // much subtler than v7-ux-6 (40 % desaturated, 32 %, <= 56) for a typical mid-tone poster
    H::Rgb mid = H::ambient({160, 120, 90}, page);
    const float oldMax = std::min(56.0f, (160 + (0.2126f * 160 + 0.7152f * 120 + 0.0722f * 90 - 160) * 0.4f) * 0.32f);
    CHECK(std::max({mid.r, mid.g, mid.b}) < oldMax * 0.7f);
    // white: capped dark grey; black: the page itself
    H::Rgb white = H::ambient({255, 255, 255}, page);
    CHECK(white.r <= H::AMBIENT_MAX && std::fabs(white.r - white.b) <= 1);
    H::Rgb black = H::ambient({0, 0, 0}, page);
    CHECK(black.r == page.r && black.g == page.g && black.b == page.b);
    // the levels: a real backdrop ~42 % (unchanged), the poster nearly untouched (unchanged)
    const float backdrop = layout::home::HERO_BACKDROP_TINT / 255.0f * (1 - layout::home::HERO_BACKDROP_SHADE / 255.0f);
    CHECK(backdrop > 0.40f && backdrop < 0.45f);
    CHECK(layout::home::SHOWCASE_TINT == 240 && layout::home::SHOWCASE_RADIUS == 12);
}

TEST(hero_art_tint_stays_around_the_poster) {
    namespace H = images::hero;
    // the usual 512 x 768 texture: the poster at 1307..1680 x 115..675; the tint's core is exactly behind it (to the
    // screen edge), eased out 110 px above / below and to the left - but never into the text column (x 1096)
    H::Showcase p = H::showcase(512, 768);
    H::AmbientArea a = H::ambientArea(p);
    CHECK(a.shown && a.coreX == p.x && a.coreY == p.y && a.coreW == p.w && a.coreH == p.h);   // solid: the poster only
    CHECK(a.fadeY == H::AMBIENT_SPREAD_Y && a.fadeLeft <= H::AMBIENT_SPREAD_X && a.fadeRight == H::AMBIENT_SPREAD_RIGHT);
    CHECK(a.coreX - a.fadeLeft >= layout::home::HERO_TEXT_RIGHT);
    CHECK(a.coreX + a.coreW + a.fadeRight <= 1920);
    CHECK(a.coreY - a.fadeY >= 0 && a.coreY + a.coreH + a.fadeY <= layout::home::BACKDROP_H);
    // far less of the hero than the v7-ux-6 field (the whole 1320 x 800 box): the solid part is the poster's own
    // rectangle (hidden behind it), and with its fades the tint stays within about half of the box
    const float box = (1920 - layout::home::BACKDROP_X) * layout::home::BACKDROP_H;
    CHECK(a.coreW * a.coreH < 0.25f * box);
    const float area = (a.fadeLeft + a.coreW + a.fadeRight) * (a.coreH + 2 * a.fadeY);
    CHECK(area < 0.55f * box);
    // every poster size: the same rules
    for (int w = 150; w <= 512; w += 61) {
        for (int h = 300; h <= 768; h += 67) {
            H::Showcase s = H::showcase(w, h);
            H::AmbientArea t = H::ambientArea(s);
            CHECK(t.shown && t.coreX - t.fadeLeft >= H::TEXT_RIGHT - 0.01f && t.coreY - t.fadeY >= 0);
            CHECK(t.coreY + t.coreH + t.fadeY <= layout::home::BACKDROP_H);
        }
    }
    // no poster shown (too small / none): no tint at all - the clean dark hero
    CHECK(!H::ambientArea(H::showcase(150, 200)).shown && !H::ambientArea(H::Showcase()).shown);
    CHECK(H::TEXT_RIGHT == layout::home::HERO_TEXT_RIGHT);
}

namespace {
    // the image loader as Home sees it: requests finish later, results are cached per URL, get() is the only way to
    // show something - so a late result for an earlier hero can never be shown for the current one
    struct FakeLoader {
        std::set<std::string> cache;
        std::deque<std::string> pending;
        int loads = 0;

        void want(const std::string &url) {
            if (!cache.count(url) && std::find(pending.begin(), pending.end(), url) == pending.end()) {
                pending.push_back(url);
            }
        }

        void finishOne() {
            if (!pending.empty()) {
                cache.insert(pending.front());
                pending.pop_front();
                loads++;
            }
        }
    };

    // Home's bind for a hero without a backdrop: the URL drawn in the showcase, or "" (the clean dark hero)
    std::string bind(FakeLoader &l, const std::string &listUrl, const std::string &coverUrl = "") {
        Inputs in;
        in.posterKnown = !listUrl.empty();
        in.posterLoaded = in.posterKnown && l.cache.count(listUrl) > 0;
        in.coverKnown = !coverUrl.empty();
        in.coverLoaded = in.coverKnown && l.cache.count(coverUrl) > 0;
        Plan p = images::hero::plan(in);
        if (p.wantCover) {
            l.want(coverUrl);
        }
        if (p.wantPoster) {
            l.want(listUrl);
        }
        return p.show == Show::Cover ? coverUrl : p.show == Show::Poster ? listUrl : "";
    }
}

TEST(hero_art_cache_stale_results_and_repeated_home_visits) {
    FakeLoader l;
    CHECK(bind(l, "a").empty() && l.pending.size() == 1);   // cache miss: requested, nothing drawn meanwhile
    l.finishOne();
    CHECK(bind(l, "a") == "a");
    // the details arrive with a bigger cover: the list image stays until it is loaded, then the cover
    CHECK(bind(l, "a", "a-big") == "a" && l.pending.size() == 1);
    l.finishOne();
    CHECK(bind(l, "a", "a-big") == "a-big");
    for (int i = 0; i < 10; i++) {
        CHECK(bind(l, "a", "a-big") == "a-big");             // Home left and entered again: cache hits
    }
    CHECK(l.loads == 2 && l.pending.empty());
    CHECK(bind(l, "b").empty() && bind(l, "c").empty());     // rapid hero changes
    l.finishOne();                                           // "b" arrives while the hero is "c": never shown
    CHECK(bind(l, "c").empty());
    l.finishOne();
    CHECK(bind(l, "c") == "c" && bind(l, "b") == "b" && l.loads == 4);
    CHECK(bind(l, "").empty() && l.pending.empty());         // no poster: nothing requested
}

// ---- detail pages (v7-ux-14): no backdrop, no tint around the poster - the clean dark page and the sharp framed
// poster (any tint read as fog on the TV). The poster keeps Home's showcase shadow.

TEST(detail_poster_frame_like_the_home_showcase) {
    namespace D = layout::detail;
    CHECK(D::POSTER_SHADOW_Y == layout::home::SHOWCASE_SHADOW_Y
          && D::POSTER_SHADOW_ALPHA == layout::home::SHOWCASE_SHADOW_ALPHA);
    // Home's halo rule is unchanged (the detail pages simply do not use it)
    images::hero::Showcase p = images::hero::showcase(512, 768);
    CHECK(images::hero::ambientArea(p).shown);
}
