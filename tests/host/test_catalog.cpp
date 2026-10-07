// Catalog integrity: identity preservation, parser rejection counts, unknown categories, cache round trip,
// sorting (incl. inside a category) and the full audit on the real provider responses when available.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <set>

#include "check.h"
#include "../../src/iptv/catalog.h"
#include "../../src/iptv/xtream.h"
#include "../../src/platform/fs.h"
#include "../../src/storage/catalog_cache.h"

using namespace iptv;

namespace {
    std::string tempDir(const char *name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", name);
        fs::ensureDir(d);
        return d;
    }

    double msSince(std::chrono::steady_clock::time_point t0) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    template<typename T>
    std::set<std::string> ids(const std::vector<T> &items) {
        std::set<std::string> out;
        for (const auto &t: items) {
            out.insert(itemId(t));
        }
        return out;
    }

    // provider-shaped list with every edge case the parser must survive
    const char *EDGE_MOVIES = R"([
      {"name":"Same Title 2020","stream_id":1,"category_id":"10","stream_icon":"http://i/1.jpg","container_extension":"mkv","rating":7,"added":"100"},
      {"name":"Same Title 2020","stream_id":2,"category_id":"10","stream_icon":"","container_extension":"mkv","rating":0,"added":"300"},
      {"name":"No Category","stream_id":3,"stream_icon":null,"container_extension":"mp4","added":"200"},
      {"name":"Unknown Category 1999","stream_id":"4","category_id":"999","container_extension":"","rating":"8.5"},
      {"name":"","stream_id":5,"category_id":"10"},
      {"name":"No Id","category_id":"10"},
      {"name":"Repeated id","stream_id":1,"category_id":"11"},
      "not an object",
      42,
      {"name":"Nested fields","stream_id":6,"category_id":"11","backdrop_path":["a","b"],"info":{"x":1},"rating":"bogus"}
    ])";

    const char *EDGE_CATEGORIES = R"([{"category_id":"10","category_name":"Action"},
      {"category_id":"11","category_name":"Drama"},{"category_id":"12","category_name":"Empty"}])";
}

TEST(catalog_identity_and_rejections) {
    std::vector<Movie> movies;
    std::vector<Category> cats;
    std::string err;
    ParseStats st;
    CHECK(xtream::parseCategories(EDGE_CATEGORIES, cats, err));
    CHECK(xtream::parseVodStreams(EDGE_MOVIES, movies, err, &st));
    CHECK_EQ(st.raw, 10);
    CHECK_EQ(st.parsed, 6);
    CHECK_EQ(st.rejectedMissingId, 1);
    CHECK_EQ(st.rejectedDuplicateId, 1);
    CHECK_EQ(st.rejectedNotObject, 2);
    CHECK_EQ(st.parsed + st.rejected(), st.raw);
    CHECK_EQ(st.missingName, 1);
    CHECK_EQ(st.missingCategory, 1);
    CHECK_EQ(st.missingPoster, 5);          // "", null, missing x3
    CHECK_EQ(st.missingExtension, 3);
    // identity is the stream id: duplicate titles with different ids are both kept; a repeated id keeps the first
    CHECK(ids(movies) == std::set<std::string>({"1", "2", "3", "4", "5", "6"}));
    CHECK(movies[0].title == "Same Title" && movies[1].title == "Same Title");
    CHECK(movies[0].categoryId == "10");    // the first "1", not the repeat in Drama
    CHECK(movies[4].name == "Movie 5");     // no name: still listed and playable
    CHECK(movies[5].rating == 0);           // malformed optional metadata does not drop the item

    MovieCatalog c;
    c.prepare(cats, movies);
    const CatalogDiagnostics &d = c.diagnostics();
    CHECK_EQ(d.visible, 6);
    CHECK_EQ(d.indexed, 6);
    CHECK_EQ(d.uncategorized, 2);           // missing + unknown category
    // categories: Action, Drama, Uncategorized (empty "Empty" hidden)
    CHECK_EQ(c.categories().size(), (size_t) 3);
    CHECK(c.categories().back().id == UNCATEGORIZED_ID && c.categories().back().name == "Uncategorized");
    std::vector<int> unc = c.inCategory(UNCATEGORIZED_ID);
    CHECK(unc.size() == 2 && c.items()[(size_t) unc[0]].streamId == "3" && c.items()[(size_t) unc[1]].streamId == "4");
    // every item is reachable: All, and exactly one category row
    size_t inRows = 0;
    for (const auto &cat: c.categories()) {
        inRows += c.inCategory(cat.id).size();
    }
    CHECK_EQ(inRows, c.size());
    CHECK_EQ(c.all().size(), (size_t) 6);
    // and searchable, uncategorized ones too
    CHECK(c.search("unknown category").size() == 1 && c.search("no category").size() == 1);
    CHECK(c.search("same title").size() == 2);
    CHECK(c.find("4") && c.find("4")->year == 1999 && c.find("4")->rating > 8.4f);
    // series and live parsers report the same way
    std::vector<Series> series;
    ParseStats ss;
    CHECK(xtream::parseSeriesList(R"([{"series_id":7,"name":"A"},{"name":"no id"},{"series_id":7,"name":"dup"},
        {"series_id":8,"name":"B","category_id":"1","cover":"c"}])", series, err, &ss));
    CHECK(ss.raw == 4 && ss.parsed == 2 && ss.rejectedMissingId == 1 && ss.rejectedDuplicateId == 1 && ss.missingCategory == 1);
    std::vector<LiveChannel> live;
    ParseStats ls;
    CHECK(xtream::parseLiveStreams(R"([{"stream_id":1,"name":"A"},{"stream_id":"","name":"B"}])", live, err, &ls));
    CHECK(ls.raw == 2 && ls.parsed == 1 && ls.rejectedMissingId == 1);
}

TEST(catalog_cache_round_trip_identity) {
    std::string dir = tempDir("cache_roundtrip");
    CatalogCache cache(dir);
    cache.clearProfile("p1");
    std::vector<Movie> before;
    std::string err;
    CHECK(xtream::parseVodStreams(EDGE_MOVIES, before, err));
    CHECK(cache.save("p1", "vod_streams", EDGE_MOVIES, 1234, (int) before.size()));
    CHECK(cache.save("p1", "vod_categories", EDGE_CATEGORIES, 1234));
    std::string body;
    CatalogCache::Meta meta;
    CHECK(cache.load("p1", "vod_streams", body, meta));
    CHECK(meta.savedAt == 1234 && meta.items == (int) before.size());
    std::vector<Movie> after;
    CHECK(xtream::parseVodStreams(body, after, err));
    CHECK(ids(before) == ids(after) && after.size() == before.size());   // the exact identity set

    std::string base = fs::join(fs::join(fs::join(dir, "cache"), "p1"), "vod_streams");
    // damaged body (same size, different bytes): rejected by the checksum and deleted
    std::string damaged = body;
    damaged[damaged.size() / 2] ^= 0x20;
    CHECK(fs::writeFileReplace(base + ".json", damaged));
    CHECK(!cache.load("p1", "vod_streams", body, meta));
    CHECK(!fs::exists(base + ".json") && !fs::exists(base + ".meta"));
    // truncated body: rejected
    CHECK(cache.save("p1", "vod_streams", EDGE_MOVIES, 1234, (int) before.size()));
    CHECK(fs::writeFileReplace(base + ".json", std::string(EDGE_MOVIES).substr(0, 50)));
    CHECK(!cache.load("p1", "vod_streams", body, meta));
    // a schema 1 copy (Checkpoint 2) is invalidated, never partially loaded
    CHECK(fs::writeFileReplace(base + ".json", EDGE_MOVIES));
    CHECK(fs::writeFileAtomic(base + ".meta", "{\"version\":1,\"savedAt\":5,\"bytes\":"
                                              + std::to_string(std::string(EDGE_MOVIES).size()) + "}"));
    CHECK(!cache.load("p1", "vod_streams", body, meta));
    CHECK(!fs::exists(base + ".json"));
    int64_t at = 0;
    CHECK(!cache.load("p1", "missing", body, at));
    CHECK(!cache.save("../evil", "x", body, 1));
    cache.clearProfile("p1");
}

TEST(catalog_sorting) {
    std::vector<Movie> m(6);
    const char *names[] = {"\xC3\x87" "\xC4\xB1lg\xC4\xB1n 2001", "ceviz 1999", "Dune 2021", "alpha", "Zorro 1975", "beta 2010"};
    float ratings[] = {7.5f, 0, 8.1f, 6.0f, 7.5f, 0};
    int64_t added[] = {500, 100, 600, 0, 300, 200};
    for (int i = 0; i < 6; i++) {
        m[(size_t) i].streamId = std::to_string(i);
        m[(size_t) i].name = names[i];
        xtream::splitTitleYear(m[(size_t) i].name, m[(size_t) i].title, m[(size_t) i].year);
        m[(size_t) i].rating = ratings[i];
        m[(size_t) i].added = added[i];
        m[(size_t) i].categoryId = i % 2 ? "odd" : "even";
    }
    MovieCatalog c;
    c.prepare({{"even", "Even", ""}, {"odd", "Odd", ""}}, m);
    auto order = [&c](std::vector<int> v, SortMode mode, const std::vector<int64_t> *act = nullptr) {
        c.sort(v, mode, act);
        std::string s;
        for (int i: v) {
            s += c.items()[(size_t) i].streamId;
        }
        return s;
    };
    CHECK(order(c.all(), SortMode::Provider) == "012345");
    CHECK(order(c.all(), SortMode::TitleAZ) == "351024");      // alpha beta ceviz Çılgın(cilgin) Dune Zorro
    CHECK(order(c.all(), SortMode::TitleZA) == "420153");
    CHECK(order(c.all(), SortMode::AddedNewest) == "204513");  // no `added` (alpha) last
    CHECK(order(c.all(), SortMode::AddedOldest) == "154023");
    CHECK(order(c.all(), SortMode::YearNewest) == "250143");   // 2021 2010 2001 1999 1975, no year last
    CHECK(order(c.all(), SortMode::YearOldest) == "410523");
    CHECK(order(c.all(), SortMode::RatingHigh) == "204315");   // ties keep provider order, unrated last
    std::vector<int64_t> activity = {0, 0, 0, 7, 0, 9};
    CHECK(order(c.all(), SortMode::RecentlyWatched, &activity) == "530124");
    // sorting applies inside the selected category
    CHECK(order(c.inCategory("odd"), SortMode::TitleAZ) == "351");
    CHECK(order(c.inCategory("even"), SortMode::RatingHigh) == "204");
    // modes the metadata supports
    CHECK(c.sortSupport().added && c.sortSupport().year && c.sortSupport().rating);
    std::vector<Series> s(3);
    for (int i = 0; i < 3; i++) {
        s[(size_t) i].seriesId = std::to_string(i);
        s[(size_t) i].name = s[(size_t) i].title = "S" + std::to_string(i);
    }
    SeriesCatalog sc;
    sc.prepare({}, s);
    CHECK(!sc.sortSupport().added && !sc.sortSupport().year && !sc.sortSupport().rating);
    CHECK(!sc.sortSupport().supports(SortMode::RatingHigh) && sc.sortSupport().supports(SortMode::TitleAZ));
    CHECK(std::string(sortModeName(SortMode::AddedNewest, ContentType::Series)) == "Recently updated");
    CHECK(std::string(sortModeName(SortMode::AddedNewest, ContentType::Movie)) == "Newest added");
    CHECK(sortModeFromKey(sortModeKey(SortMode::YearOldest)) == SortMode::YearOldest);
    CHECK(sortModeFromKey("nonsense") == SortMode::Provider);
}

namespace {
    // the whole pipeline on one real response: raw -> parse -> catalog -> cache save -> reload -> reparse
    template<typename T, typename Catalog>
    void auditReal(const char *what, const std::string &dir, const char *catFile, const char *listFile,
                   bool (*parse)(const std::string &, std::vector<T> &, std::string &, ParseStats *)) {
        std::string catBody, listBody, err;
        if (!fs::readFile(fs::join(dir, catFile), catBody, 8u << 20)
            || !fs::readFile(fs::join(dir, listFile), listBody, 128u << 20)) {
            std::printf("     (no %s sample)\n", what);
            return;
        }
        std::vector<Category> cats;
        std::vector<T> items;
        ParseStats st;
        auto t0 = std::chrono::steady_clock::now();
        CHECK(xtream::parseCategories(catBody, cats, err));
        CHECK(parse(listBody, items, err, &st));
        double parseMs = msSince(t0);
        std::set<std::string> parsedIds = ids(items);
        CHECK_EQ(parsedIds.size(), items.size());

        CatalogCache cache(tempDir("cache_real"));
        CHECK(cache.save("audit", listFile[4] == 'v' ? "vod_streams" : "series", listBody, 1, (int) items.size()));
        std::string reloaded;
        CatalogCache::Meta meta;
        CHECK(cache.load("audit", listFile[4] == 'v' ? "vod_streams" : "series", reloaded, meta));
        std::vector<T> again;
        CHECK(parse(reloaded, again, err, nullptr));
        CHECK(ids(again) == parsedIds && meta.items == (int) again.size());

        Catalog c;
        int missingYear = 0, missingRating = 0;
        for (const auto &t: items) {
            missingYear += t.year == 0;
            missingRating += t.rating <= 0;
        }
        auto t1 = std::chrono::steady_clock::now();
        c.assign(cats, std::move(items));
        c.diagnostics().parse = st;   // what the loader records (XtreamService)
        c.diagnostics().cached = meta.items;
        double assignMs = msSince(t1);
        auto t2 = std::chrono::steady_clock::now();
        c.buildSearch();
        double indexMs = msSince(t2);
        auto t3 = std::chrono::steady_clock::now();
        c.buildSortKeys();
        double keysMs = msSince(t3);
        const CatalogDiagnostics &d = c.diagnostics();
        size_t inRows = 0;
        for (const auto &cat: c.categories()) {
            inRows += c.inCategory(cat.id).size();
        }
        CHECK_EQ(inRows, c.size());
        CHECK_EQ(d.visible, st.parsed);
        CHECK_EQ(d.indexed, st.parsed);
        // sort timings: All and the largest category, every supported mode
        double worstSort = 0;
        for (int m = 0; m < (int) SortMode::Count; m++) {
            if (!c.sortSupport().supports((SortMode) m)) {
                continue;
            }
            std::vector<int64_t> activity(c.size(), 0);
            std::vector<int> all = c.all();
            auto t4 = std::chrono::steady_clock::now();
            c.sort(all, (SortMode) m, &activity);
            worstSort = std::max(worstSort, msSince(t4));
            CHECK_EQ(all.size(), c.size());
        }
        std::printf("     AUDIT %s: raw %d | parsed %d | unique ids %zu | rejected %d (no id %d, duplicate id %d, "
                    "not object %d) | cached %d | reloaded %zu | visible %d | indexed %d | uncategorized %d | dropped %d\n",
                    what, st.raw, st.parsed, parsedIds.size(), st.rejected(), st.rejectedMissingId,
                    st.rejectedDuplicateId, st.rejectedNotObject, meta.items, again.size(), d.visible, d.indexed,
                    d.uncategorized, d.dropped());
        std::printf("           missing: name %d, category %d (unknown category %d), poster %d, extension %d, "
                    "year %d, rating %d | categories %d | name with year alias %d\n", st.missingName,
                    st.missingCategory, d.uncategorized - st.missingCategory, st.missingPoster, st.missingExtension,
                    missingYear, missingRating, d.categories, st.titleYearAliases);
        std::printf("           parse %.0f ms, catalog %.0f ms, search index %.0f ms (%.2f MB), sort keys %.0f ms, "
                    "worst full sort %.1f ms | sort modes:", parseMs, assignMs, indexMs,
                    (double) d.indexBytes / (1024.0 * 1024.0), keysMs, worstSort);
        for (int m = 0; m < (int) SortMode::Count; m++) {
            if (c.sortSupport().supports((SortMode) m)) {
                std::printf(" %s", sortModeKey((SortMode) m));
            }
        }
        std::printf("\n");
    }
}

TEST(catalog_real_provider_audit_if_available) {
    const char *dir = std::getenv("PS4IPTV_SAMPLES");
    if (dir == nullptr) {
        std::printf("  (skipped: PS4IPTV_SAMPLES not set)\n");
        return;
    }
    auditReal<Movie, MovieCatalog>("movies", dir, "get_vod_categories.json", "get_vod_streams.json",
                                   &xtream::parseVodStreams);
    auditReal<Series, SeriesCatalog>("series", dir, "get_series_categories.json", "get_series.json",
                                     &xtream::parseSeriesList);
    std::string body, cbody, err;
    std::vector<LiveChannel> live;
    std::vector<Category> cats;
    ParseStats st;
    if (fs::readFile(fs::join(dir, "get_live_streams.json"), body, 64u << 20)
        && fs::readFile(fs::join(dir, "get_live_categories.json"), cbody, 1u << 20)) {
        CHECK(xtream::parseLiveStreams(body, live, err, &st) && xtream::parseCategories(cbody, cats, err));
        auto t0 = std::chrono::steady_clock::now();
        LiveCatalog lc;
        lc.assign(cats, live);
        double ms = msSince(t0);
        std::printf("     AUDIT live: raw %d | parsed %d | rejected %d | indexed %d | missing logo %d | catalog + index %.1f ms\n",
                    st.raw, st.parsed, st.rejected(), lc.searchIndex().size(), st.missingPoster, ms);
        CHECK_EQ(lc.searchIndex().size(), st.parsed);
    }
}
