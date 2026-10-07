#include <cstdlib>

#include "check.h"
#include "../../src/core/json.h"
#include "../../src/iptv/catalog.h"
#include "../../src/iptv/xtream.h"
#include "../../src/platform/fs.h"
#include "../../src/storage/catalog_cache.h"
#include "../../src/storage/library_store.h"

using namespace iptv;

static std::string tmp(const char *name) {
    const char *base = std::getenv("PS4IPTV_TEST_TMP");
    std::string d = fs::join(base ? base : ".", name);
    fs::ensureDir(d);
    for (const auto &e: fs::listDir(d)) {
        if (e.dir) {
            for (const auto &f: fs::listDir(fs::join(d, e.name))) {
                fs::removeFile(fs::join(fs::join(d, e.name), f.name));
            }
        } else {
            fs::removeFile(fs::join(d, e.name));
        }
    }
    return d;
}

TEST(json_stream_flat_objects) {
    int n = 0;
    std::string err;
    std::vector<std::string> ids;
    CHECK(json::forEachObject(R"([{"a":1,"b":"x","n":null,"t":true,"arr":[1,{"z":2}],"o":{"k":"v"}},7,"s",
        {"a":"2"},{}])", [&](const json::FlatObject &o) {
        n++;
        ids.push_back(o.get("a"));
        if (n == 1) {
            CHECK(o.get("b") == "x");
            CHECK(o.get("n").empty());
            CHECK(o.getBool("t"));
            CHECK(o.has("arr") && o.get("arr").empty());
            CHECK(o.has("o"));
            CHECK_EQ(o.getInt("a"), (int64_t) 1);
            CHECK_EQ(o.getInt("missing", 9), (int64_t) 9);
        }
        return true;
    }, &err));
    CHECK_EQ(n, 3);
    CHECK(ids[1] == "2" && ids[2].empty());
    // early stop
    n = 0;
    CHECK(json::forEachObject("[{},{},{}]", [&](const json::FlatObject &) { return ++n < 2; }));
    CHECK_EQ(n, 2);
    CHECK(!json::forEachObject("{\"a\":1}", [](const json::FlatObject &) { return true; }, &err));
    CHECK(!json::forEachObject("[{\"a\":1},", [](const json::FlatObject &) { return true; }, &err));
    CHECK(json::forEachObject("[]", [](const json::FlatObject &) { return true; }));
}

TEST(xtream_live_streams) {
    std::vector<LiveChannel> ch;
    std::string err;
    CHECK(xtream::parseLiveStreams(R"([
        {"num":1,"name":"TRT 1 HD","stream_type":"live","stream_id":101,"stream_icon":"https://x/1.png",
         "epg_channel_id":"trt1.tr","added":"1700000000","category_id":"5","custom_sid":null,"tv_archive":0,
         "direct_source":"","tv_archive_duration":0},
        {"num":"2","name":"  Şow TV ","stream_id":"102","category_id":"5","tv_archive":"1"},
        {"name":"no id"},
        {"stream_id":103,"name":"","category_id":"6","category_ids":[6,7]}])", ch, err));
    CHECK_EQ(ch.size(), (size_t) 3);
    CHECK(ch[0].streamId == "101" && ch[0].name == "TRT 1 HD" && ch[0].categoryId == "5");
    CHECK(ch[0].icon == "https://x/1.png" && ch[0].epgId == "trt1.tr" && ch[0].added == 1700000000 && ch[0].num == 1);
    CHECK(ch[1].name == "\xC5\x9Eow TV" && ch[1].num == 2 && ch[1].archive);
    CHECK(ch[2].name == "Channel 103");
    CHECK(!xtream::parseLiveStreams(R"({"user_info":{"auth":0}})", ch, err));
}

TEST(live_catalog_indices_and_search) {
    std::vector<LiveChannel> ch;
    auto add = [&](const char *id, const char *name, const char *cat) {
        LiveChannel c;
        c.streamId = id;
        c.name = name;
        c.categoryId = cat;
        ch.push_back(c);
    };
    add("1", "TR: TRT 1 HD", "news");
    add("2", "TRT Spor", "sport");
    add("3", "\xC5\x9E" "ehir TV", "news");        // Şehir TV
    add("4", "Bein Sports 1", "sport");
    add("5", "Kanal \xC4\xB0stanbul", "news");     // Kanal İstanbul
    add("6", "XTRTX", "other");
    LiveCatalog cat;
    cat.assign({}, ch);
    CHECK_EQ(cat.inCategory("news").size(), (size_t) 3);
    CHECK_EQ(cat.inCategory("none").size(), (size_t) 0);
    CHECK(cat.find("4") && cat.find("4")->name == "Bein Sports 1");
    std::vector<int> r = cat.search("trt");
    CHECK_EQ(r.size(), (size_t) 3);
    CHECK(r[0] == 1);   // prefix "TRT Spor"
    CHECK(r[1] == 0);   // word start "TR: TRT 1 HD"
    CHECK(r[2] == 5);   // substring "XTRTX"
    CHECK(cat.search("sehir").size() == 1);            // ASCII keyboard input finds Şehir
    CHECK(cat.search("\xC5\x9F" "ehir").size() == 1);  // şehir too
    CHECK(cat.search("istanbul").size() == 1);  // İ folds to i
    CHECK(cat.search("  ").empty());
    std::set<std::string> fav = {"5", "2", "nope"};
    std::vector<int> f = cat.favorites(fav);
    CHECK(f.size() == 2 && f[0] == 1 && f[1] == 4);
}

TEST(real_provider_live_streams_if_available) {
    // build/xtream-samples is filled by scripts/fetch-xtream-samples.py (not committed)
    const char *dir = std::getenv("PS4IPTV_SAMPLES");
    if (!dir) {
        return;
    }
    std::string body;
    if (!fs::readFile(fs::join(dir, "get_live_streams.json"), body, 128u * 1024 * 1024)) {
        return;
    }
    std::vector<LiveChannel> ch;
    std::string err;
    CHECK(xtream::parseLiveStreams(body, ch, err));
    CHECK(!ch.empty());
    std::string catBody;
    std::vector<Category> cats;
    if (fs::readFile(fs::join(dir, "get_live_categories.json"), catBody, 8u * 1024 * 1024)) {
        CHECK(xtream::parseCategories(catBody, cats, err));
    }
    LiveCatalog catalog;
    size_t total = ch.size();
    catalog.assign(cats, ch);
    size_t inCats = 0;
    for (const auto &c: cats) {
        inCats += catalog.inCategory(c.id).size();
    }
    CHECK_EQ(inCats, total);  // every channel belongs to a listed category
    std::string authBody;
    if (fs::readFile(fs::join(dir, "auth.json"), authBody, 1024 * 1024)) {
        AuthResult a = xtream::parseAuth(200, authBody, 0);
        CHECK(a.status == AuthStatus::Ok);
        CHECK(a.account.maxConnections >= 1);
    }
    std::printf("     real provider sample: %zu categories, %zu channels parsed\n", cats.size(), total);
}

TEST(library_favorites_and_history) {
    std::string dir = tmp("library");
    LibraryStore s(dir);
    s.load();
    s.setProfile("p1");
    CHECK(!s.isFavorite(ContentType::Live, "1"));
    CHECK(s.toggleFavorite(ContentType::Live, "1"));
    CHECK(s.isFavorite(ContentType::Live, "1"));
    CHECK(!s.isFavorite(ContentType::Movie, "1"));
    s.toggleFavorite(ContentType::Movie, "m9");
    for (int i = 0; i < 130; i++) {
        HistoryEntry e;
        e.id = std::to_string(i % 110);
        e.name = "ch";
        e.watchedAt = i;
        s.addHistory(e);
    }
    CHECK_EQ(s.history().size(), LibraryStore::HISTORY_LIMIT);
    CHECK(s.history()[0].id == "19" && s.history()[0].watchedAt == 129);  // latest first, deduplicated
    s.setProfile("p2");
    CHECK(s.history().empty());
    CHECK(!s.isFavorite(ContentType::Live, "1"));
    s.setProfile("p1");
    CHECK(s.save());

    LibraryStore t(dir);
    t.load();
    t.setProfile("p1");
    CHECK(t.isFavorite(ContentType::Live, "1"));
    CHECK(t.isFavorite(ContentType::Movie, "m9"));
    CHECK(!t.toggleFavorite(ContentType::Live, "1"));
    CHECK(t.history().size() == LibraryStore::HISTORY_LIMIT && t.history()[0].id == "19");
    t.removeProfile("p1");
    CHECK(t.history().empty());
}

TEST(catalog_cache_roundtrip_and_rejects) {
    std::string dir = tmp("cachetest");
    CatalogCache c(dir);
    std::string body = "[{\"stream_id\":1}]";
    CHECK(c.save("p1", "live_streams", body, 1234));
    std::string out;
    int64_t at = 0;
    CHECK(c.load("p1", "live_streams", out, at));
    CHECK(out == body && at == 1234);
    CHECK(!c.load("p1", "missing", out, at));
    CHECK(!c.save("../evil", "x", body, 1));
    CHECK(!c.load("p1", "../x", out, at));
    // body/meta mismatch (interrupted write) is rejected
    CHECK(fs::writeFileAtomic(fs::join(fs::join(fs::join(dir, "cache"), "p1"), "live_streams.json"), "[]"));
    CHECK(!c.load("p1", "live_streams", out, at));
    c.clearProfile("p1");
    CHECK(!c.load("p1", "live_streams", out, at));
}
