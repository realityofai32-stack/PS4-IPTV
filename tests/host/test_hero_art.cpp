// The Home hero's artwork (images/hero_art.h): the generated cinematic background for every kind of provider poster
// (portrait, small, very small, landscape, wide, unusual ratios, transparent, invalid), the artwork priority plan
// (usable / missing / failed / unusable backdrops) and the caching behaviour Home relies on, simulated with an
// asynchronous loader (cache hit / miss, rapid hero changes, a stale result, repeated Home enter / exit). It also
// measures the generation time and the texture memory.

#include <chrono>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/images/hero_art.h"
#include "../../src/images/image_decode.h"

using namespace images;
using images::hero::Crop;
using images::hero::Mode;
using images::hero::Show;

namespace {
    // a poster-like test image: a vertical gradient, a bright "title" block at 30 % and fine noise (artefacts)
    Image poster(int w, int h, uint8_t alpha = 255) {
        Image img;
        img.w = w;
        img.h = h;
        img.rgba.resize((size_t) w * h * 4);
        uint32_t seed = 12345;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                seed = seed * 1103515245u + 12345u;
                int noise = (int) ((seed >> 16) % 41) - 20;
                bool title = y > h * 0.25 && y < h * 0.38 && x > w * 0.1 && x < w * 0.9;
                uint8_t *p = &img.rgba[((size_t) y * w + x) * 4];
                p[0] = (uint8_t) std::min(255, std::max(0, (title ? 230 : 40 + 150 * y / h) + noise));
                p[1] = (uint8_t) std::min(255, std::max(0, (title ? 200 : 60 + 60 * x / w) + noise));
                p[2] = (uint8_t) std::min(255, std::max(0, (title ? 90 : 120) + noise));
                p[3] = alpha;
            }
        }
        return img;
    }

    double luma(const Image &img, int x, int y) {
        const uint8_t *p = &img.rgba[((size_t) y * img.w + x) * 4];
        return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    }

    double columnLuma(const Image &img, int x) {
        double s = 0;
        for (int y = 0; y < img.h; y++) {
            s += luma(img, x, y);
        }
        return s / img.h;
    }

    // largest step between neighbouring pixels (a blurred image has no hard edges, no noise)
    int maxStep(const Image &img) {
        int m = 0;
        for (int y = 0; y < img.h; y++) {
            for (int x = 1; x < img.w; x++) {
                for (int c = 0; c < 3; c++) {
                    int a = img.rgba[((size_t) y * img.w + x - 1) * 4 + c];
                    int b = img.rgba[((size_t) y * img.w + x) * 4 + c];
                    m = std::max(m, std::abs(a - b));
                }
            }
        }
        return m;
    }

    bool opaque(const Image &img) {
        for (size_t i = 3; i < img.rgba.size(); i += 4) {
            if (img.rgba[i] != 255) {
                return false;
            }
        }
        return true;
    }

    double msOf(const Image &src, int runs, hero::Result &out) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < runs; i++) {
            out = hero::generate(src);
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / runs;
    }
}

TEST(hero_art_portrait_poster_becomes_a_blurred_16_9_background) {
    Image src = poster(400, 600);
    hero::Result r = hero::generate(src);
    CHECK(r.mode == Mode::Blurred && r.crop == Crop::Band);
    CHECK(r.image.valid() && r.image.w == hero::OUT_W && r.image.h == hero::OUT_H && opaque(r.image));
    // the band: the full width, 16:9, around the upper third (the title area), inside the image
    CHECK(r.cropW == 400 && std::abs(r.cropH - 225) <= 1);
    CHECK(r.cropY >= 0 && r.cropY + r.cropH <= 600);
    CHECK(std::fabs((r.cropY + r.cropH / 2.0) / 600 - hero::BAND_CENTER) < 0.02);
    // blurred: no noise, no hard edges (the source has +-20 noise and a sharp title block)
    CHECK(maxStep(r.image) <= 6);
    // darker towards the left (the text column), a visible picture on the right
    CHECK(columnLuma(r.image, 4) < columnLuma(r.image, hero::OUT_W - 5) * 0.7);
    // darkened: never brighter than the source's brightest
    double maxSrc = 0, maxOut = 0;
    for (int y = 0; y < src.h; y += 3) {
        for (int x = 0; x < src.w; x += 3) {
            maxSrc = std::max(maxSrc, luma(src, x, y));
        }
    }
    for (int y = 0; y < r.image.h; y += 3) {
        for (int x = 0; x < r.image.w; x += 3) {
            maxOut = std::max(maxOut, luma(r.image, x, y));
        }
    }
    CHECK(maxOut < maxSrc * hero::DARKEN + 2);
    // moderately desaturated: the colours move towards grey
    CHECK(r.ambient.r > 0 && r.ambient.g > 0 && r.ambient.b > 0);
}

TEST(hero_art_small_and_very_small_posters) {
    // small but recognisable (a 120 x 180 thumbnail): still the blur, which hides how small it is
    hero::Result small = hero::generate(poster(120, 180));
    CHECK(small.mode == Mode::Blurred && small.image.w == hero::OUT_W && maxStep(small.image) <= 6);
    // very small (60 x 90): nothing recognisable - ambient colour only, no enlarged picture
    hero::Result tiny = hero::generate(poster(60, 90));
    CHECK(tiny.mode == Mode::Ambient && tiny.crop == Crop::None);
    CHECK(tiny.image.w == hero::AMBIENT_W && tiny.image.h == hero::AMBIENT_H && opaque(tiny.image));
    CHECK(maxStep(tiny.image) <= 3);   // a smooth wash
    // the wash is dark (a clean dark hero, a hint of the poster's colour)
    double maxL = 0;
    for (int x = 0; x < tiny.image.w; x += 4) {
        maxL = std::max(maxL, columnLuma(tiny.image, x));
    }
    CHECK(maxL < 110);
    CHECK(columnLuma(tiny.image, 2) < columnLuma(tiny.image, tiny.image.w - 3));
    // 1 x 1 and a long thin strip: ambient, never a crash
    CHECK(hero::generate(poster(1, 1)).mode == Mode::Ambient);
    CHECK(hero::generate(poster(2000, 40)).mode == Mode::Ambient);
}

TEST(hero_art_wide_and_unusual_aspect_ratios) {
    // a large landscape cover (real 16:9 art): sharp, cropped to 16:9, at most 1280 x 720, never enlarged
    hero::Result wide = hero::generate(poster(1920, 1080));
    CHECK(wide.mode == Mode::Landscape && wide.crop == Crop::Full);
    CHECK(wide.image.w == 1280 && wide.image.h == 720);
    hero::Result w1000 = hero::generate(poster(1000, 600));   // 1.67: a band, kept at its own width
    CHECK(w1000.mode == Mode::Landscape && w1000.crop == Crop::Band && w1000.image.w == 1000 && w1000.image.h == 563);
    // a panorama (3:1): its centre
    hero::Result pano = hero::generate(poster(2400, 800));
    CHECK(pano.mode == Mode::Landscape && pano.crop == Crop::Center && pano.cropW == 1422 && pano.cropX == 489);
    // a small landscape image (800 x 450): too small to be shown sharp - the blur
    hero::Result small = hero::generate(poster(800, 450));
    CHECK(small.mode == Mode::Blurred && small.crop == Crop::Full && small.image.w == hero::OUT_W);
    // square and very tall images: a band
    hero::Result sq = hero::generate(poster(500, 500));
    CHECK(sq.mode == Mode::Blurred && sq.crop == Crop::Band && sq.cropW == 500 && sq.cropH == 281);
    hero::Result tall = hero::generate(poster(100, 1000));
    CHECK(tall.mode == Mode::Blurred && tall.crop == Crop::Band && tall.cropH == 56 && tall.cropY >= 0);
    // a transparent poster: alpha-weighted, the result is opaque (no fringes)
    hero::Result clear = hero::generate(poster(400, 600, 0));
    CHECK(clear.image.valid() && opaque(clear.image));
    // an invalid / failed decode: nothing (the plain dark hero)
    Image broken;
    broken.w = 10;
    broken.h = 10;   // no pixels
    CHECK(!hero::generate(broken).image.valid() && !hero::generate(Image()).image.valid());
}

TEST(hero_art_provider_backdrop_usability) {
    CHECK(hero::usableBackdrop(1280, 720) && hero::usableBackdrop(1920, 1080) && hero::usableBackdrop(780, 439));
    CHECK(!hero::usableBackdrop(600, 900));     // a portrait poster sent as backdrop_path
    CHECK(!hero::usableBackdrop(500, 281));     // a thumbnail
    CHECK(!hero::usableBackdrop(800, 700));     // nearly square
    CHECK(!hero::usableBackdrop(0, 0));
}

TEST(hero_art_priority_plan) {
    using hero::Inputs;
    // 1. a valid provider backdrop: shown, nothing generated
    Inputs good;
    good.backdropKnown = good.backdropLoaded = good.fallbackKnown = true;
    good.backdropW = 1280;
    good.backdropH = 720;
    hero::Plan p = hero::plan(good);
    CHECK(p.show == Show::Backdrop && p.backdropUsable && p.wantBackdrop && !p.wantGenerated);
    // ... even when a generated one is still in memory
    good.generatedLoaded = true;
    CHECK(hero::plan(good).show == Show::Backdrop);
    // 2. missing backdrop (none / an empty backdrop_path array parse to ""): the generated background
    Inputs none;
    none.fallbackKnown = true;
    p = hero::plan(none);
    CHECK(p.show == Show::Nothing && !p.wantBackdrop && p.wantGenerated);   // while it is generated: plain hero
    none.generatedLoaded = true;
    CHECK(hero::plan(none).show == Show::Generated);
    // 3. a backdrop that is loading: both wanted; the generated one is shown meanwhile when it is ready
    Inputs loading;
    loading.backdropKnown = loading.fallbackKnown = loading.generatedLoaded = true;
    p = hero::plan(loading);
    CHECK(p.wantBackdrop && p.wantGenerated && p.show == Show::Generated);
    // 4. a failed backdrop URL: the generated background, the backdrop no longer wanted
    Inputs failed = loading;
    failed.backdropFailed = true;
    p = hero::plan(failed);
    CHECK(!p.wantBackdrop && p.wantGenerated && p.show == Show::Generated);
    // 5. a backdrop that loaded but is a portrait poster / too small: unusable, the generated background instead
    Inputs bad = loading;
    bad.backdropLoaded = true;
    bad.backdropW = 600;
    bad.backdropH = 900;
    p = hero::plan(bad);
    CHECK(!p.backdropUsable && !p.wantBackdrop && p.wantGenerated && p.show == Show::Generated);
    // 6. no artwork at all: the plain dark hero, nothing requested
    p = hero::plan(Inputs());
    CHECK(p.show == Show::Nothing && !p.wantBackdrop && !p.wantGenerated);
}

namespace {
    // the image loader as Home sees it: requests complete later (worker), results are cached per URL, get() is the
    // only way to show something - so a late result for an old hero can never be shown for a new one
    struct FakeLoader {
        std::map<std::string, hero::Result> cache;
        std::deque<std::string> pending;
        std::map<std::string, Image> sources;   // what the provider would return
        int generated = 0;

        void want(const std::string &url) {
            if (!cache.count(url) && std::find(pending.begin(), pending.end(), url) == pending.end()) {
                pending.push_back(url);
            }
        }

        // a worker finishes one job
        void finishOne() {
            if (pending.empty()) {
                return;
            }
            std::string url = pending.front();
            pending.pop_front();
            generated++;
            cache[url] = hero::generate(sources[url]);
        }

        const hero::Result *get(const std::string &url) {
            auto it = cache.find(url);
            return it == cache.end() ? nullptr : &it->second;
        }
    };

    // Home's bind for the current hero: what is drawn (nullptr = plain hero)
    const hero::Result *bind(FakeLoader &l, const std::string &posterUrl) {
        hero::Inputs in;
        in.fallbackKnown = !posterUrl.empty();
        const hero::Result *g = in.fallbackKnown ? l.get(posterUrl) : nullptr;
        in.generatedLoaded = g != nullptr;
        hero::Plan p = hero::plan(in);
        if (p.wantGenerated) {
            l.want(posterUrl);
        }
        return p.show == Show::Generated ? g : nullptr;
    }
}

TEST(hero_art_cache_stale_results_and_repeated_home_visits) {
    FakeLoader l;
    l.sources["a"] = poster(400, 600);
    l.sources["b"] = poster(300, 450);
    l.sources["c"] = poster(1920, 1080);
    // cache miss: requested, nothing drawn until the worker is done (no UI stall: the bind returns at once)
    CHECK(bind(l, "a") == nullptr && l.pending.size() == 1);
    l.finishOne();
    const hero::Result *a = bind(l, "a");
    CHECK(a != nullptr && a->mode == Mode::Blurred);
    // cache hit: Home left and entered again ten times - never generated again
    for (int i = 0; i < 10; i++) {
        CHECK(bind(l, "a") == a);
    }
    CHECK(l.generated == 1 && l.pending.empty());
    // rapid hero changes (Continue Watching changed several times before any job finished)
    CHECK(bind(l, "b") == nullptr);
    CHECK(bind(l, "c") == nullptr);
    CHECK(l.pending.size() == 2);
    // a stale result: "b" finishes while the hero is already "c" - Home shows nothing for "c" yet, never "b"
    l.finishOne();
    CHECK(bind(l, "c") == nullptr);
    l.finishOne();
    const hero::Result *c = bind(l, "c");
    CHECK(c != nullptr && c->mode == Mode::Landscape);
    // back to "b": already in the cache
    CHECK(bind(l, "b") != nullptr && l.generated == 3);
    // a poster that does not decode: no artwork, plain hero (and no crash)
    l.sources["broken"] = Image();
    bind(l, "broken");
    l.finishOne();
    const hero::Result *br = bind(l, "broken");
    CHECK(br != nullptr && !br->image.valid());   // the loader drops it (Permanent): the hero stays plain
}

// generation cost and texture memory (reported in the test output; the console logs the same numbers per image)
TEST(hero_art_generation_cost) {
    struct Case {
        int w, h;
    } cases[] = {{400, 600}, {600, 900}, {853, 1280}, {1280, 1280}, {1920, 1080}, {60, 90}};
    for (const Case &c: cases) {
        Image src = poster(c.w, c.h);
        hero::Result r;
        double ms = msOf(src, 5, r);
        double kib = r.image.w * r.image.h * 4 / 1024.0;
        std::printf("  hero art %4dx%-4d -> %-18s %4dx%-3d texture %6.0f KiB, %.2f ms (host, -O2)\n", c.w, c.h,
                    hero::modeName(r.mode), r.image.w, r.image.h, kib, ms);
        CHECK(ms < 250);   // generous: the console's CPU is several times slower than the host
        if (r.mode == Mode::Blurred) {
            CHECK(kib == 900);
        }
    }
}
