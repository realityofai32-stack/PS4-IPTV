// The Home hero's artwork choice (images/hero_art.h): provider artwork only - a usable landscape backdrop, else the
// sharp poster on its colour field (its size limits: test_image_fit.cpp fit_hero_poster_*), else the clean dark hero.
// The generated blurred background of the v7-ux checkpoint is gone. Also simulated with an asynchronous loader:
// cache hit / miss, rapid hero changes, a stale result, repeated Home visits, one large texture requested at a time.

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <string>

#include "check.h"
#include "../../src/images/hero_art.h"

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
    // 1. a valid provider backdrop: shown, the poster not requested (one large texture)
    Inputs good;
    good.backdropKnown = good.backdropLoaded = good.posterKnown = true;
    good.backdropW = 1280;
    good.backdropH = 720;
    Plan p = images::hero::plan(good);
    CHECK(p.show == Show::Backdrop && p.backdropUsable && p.wantBackdrop && !p.wantPoster);
    good.posterLoaded = true;                      // even with the poster still in memory
    CHECK(images::hero::plan(good).show == Show::Backdrop);
    // 2. no backdrop (none, or an empty backdrop_path array, both parse to ""): the sharp poster
    Inputs none;
    none.posterKnown = true;
    p = images::hero::plan(none);
    CHECK(p.show == Show::Nothing && !p.wantBackdrop && p.wantPoster);   // while it loads: the clean dark hero
    none.posterLoaded = true;
    CHECK(images::hero::plan(none).show == Show::Poster);
    // 3. a backdrop loading: only the backdrop is requested; a poster still in memory stays meanwhile
    Inputs loading;
    loading.backdropKnown = loading.posterKnown = true;
    p = images::hero::plan(loading);
    CHECK(p.wantBackdrop && !p.wantPoster && p.show == Show::Nothing);
    loading.posterLoaded = true;
    CHECK(images::hero::plan(loading).show == Show::Poster);
    // 4. a failed backdrop URL: the poster
    Inputs failed;
    failed.backdropKnown = failed.backdropFailed = failed.posterKnown = true;
    p = images::hero::plan(failed);
    CHECK(!p.wantBackdrop && p.wantPoster);
    // 5. a backdrop that loaded but is a portrait poster / a thumbnail: unusable, the poster instead
    Inputs bad;
    bad.backdropKnown = bad.backdropLoaded = bad.posterKnown = bad.posterLoaded = true;
    bad.backdropW = 600;
    bad.backdropH = 900;
    p = images::hero::plan(bad);
    CHECK(!p.backdropUsable && !p.wantBackdrop && p.wantPoster && p.show == Show::Poster);
    // 6. no artwork at all: the clean dark hero, nothing requested
    p = images::hero::plan(Inputs());
    CHECK(p.show == Show::Nothing && !p.wantBackdrop && !p.wantPoster);
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

    // Home's bind for a hero without a backdrop: the poster URL drawn, or "" (the clean dark hero)
    std::string bind(FakeLoader &l, const std::string &posterUrl) {
        Inputs in;
        in.posterKnown = !posterUrl.empty();
        in.posterLoaded = in.posterKnown && l.cache.count(posterUrl) > 0;
        Plan p = images::hero::plan(in);
        if (p.wantPoster) {
            l.want(posterUrl);
        }
        return p.show == Show::Poster ? posterUrl : "";
    }
}

TEST(hero_art_cache_stale_results_and_repeated_home_visits) {
    FakeLoader l;
    CHECK(bind(l, "a").empty() && l.pending.size() == 1);   // cache miss: requested, nothing drawn meanwhile
    l.finishOne();
    CHECK(bind(l, "a") == "a");
    for (int i = 0; i < 10; i++) {
        CHECK(bind(l, "a") == "a");                          // Home left and entered again: a cache hit
    }
    CHECK(l.loads == 1 && l.pending.empty());
    CHECK(bind(l, "b").empty() && bind(l, "c").empty());     // rapid hero changes
    l.finishOne();                                           // "b" arrives while the hero is "c": never shown
    CHECK(bind(l, "c").empty());
    l.finishOne();
    CHECK(bind(l, "c") == "c" && bind(l, "b") == "b" && l.loads == 3);
    CHECK(bind(l, "").empty() && l.pending.empty());         // no poster: nothing requested
}
