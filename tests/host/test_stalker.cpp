// Stalker / MAG portals: MAC and portal address handling, identity, response parsing, mapping into the shared
// models, the saved-copy format, profile storage / migration and redaction. Responses are shaped like those of
// Ministra-compatible portals (synthetic values only).

#include <cstdlib>

#include "check.h"
#include "../../src/core/hash.h"
#include "../../src/iptv/stalker.h"
#include "../../src/platform/fs.h"
#include "../../src/platform/redact.h"
#include "../../src/storage/profile_store.h"

using namespace iptv;

namespace {
    std::string tempDir(const char *name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string dir = fs::join(base ? base : ".", name);
        fs::ensureDir(dir);
        for (const auto &e: fs::listDir(dir)) {
            fs::removeFile(fs::join(dir, e.name));
        }
        return dir;
    }

    bool hasHeader(const std::vector<std::string> &h, const std::string &line) {
        for (const auto &x: h) {
            if (x == line) {
                return true;
            }
        }
        return false;
    }
}

TEST(stalker_hash_vectors) {
    CHECK_EQ(hash::md5Hex(""), std::string("D41D8CD98F00B204E9800998ECF8427E"));
    CHECK_EQ(hash::md5Hex("abc"), std::string("900150983CD24FB0D6963F7D28E17F72"));
    CHECK_EQ(hash::sha256Hex("abc"), std::string("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    CHECK_EQ(hash::sha256Hex(""), std::string("E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"));
    // two blocks (FIPS 180-2 example)
    CHECK_EQ(hash::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1"));
    CHECK_EQ(hash::md5Hex("The quick brown fox jumps over the lazy dog"), std::string("9E107D9D372BB6826BD81D3542A419D6"));
}

TEST(stalker_mac_and_identity) {
    std::string mac;
    CHECK(stalker::normalizeMac("00:1a:79:ab:cd:ef", mac) && mac == "00:1A:79:AB:CD:EF");
    CHECK(stalker::normalizeMac("00-1A-79-AB-CD-EF", mac) && mac == "00:1A:79:AB:CD:EF");
    CHECK(stalker::normalizeMac(" 001A79ABCDEF ", mac) && mac == "00:1A:79:AB:CD:EF");
    CHECK(!stalker::normalizeMac("00:1A:79:AB:CD", mac));
    CHECK(!stalker::normalizeMac("00:1A:79:AB:CD:EF:01", mac));
    CHECK(!stalker::normalizeMac("00:1A:79:AB:CD:EG", mac));
    CHECK(!stalker::normalizeMac("", mac));
    std::string masked = stalker::maskedMac("00:1A:79:AB:CD:EF");
    CHECK(masked.find("00:1A:79") == 0 && masked.find("AB") == std::string::npos && masked.find("CD") == std::string::npos);
    stalker::Identity a = stalker::identity("00:1A:79:AB:CD:EF");
    stalker::Identity b = stalker::identity("00:1A:79:AB:CD:EF");
    CHECK_EQ(a.serial.size(), (size_t) 13);
    CHECK_EQ(a.deviceId.size(), (size_t) 64);
    CHECK(a.serial == b.serial && a.deviceId == b.deviceId && a.signature == b.signature);   // stable per MAC
    CHECK(a.deviceId != a.deviceId2);
    CHECK(stalker::identity("00:1A:79:AB:CD:EE").serial != a.serial);
}

TEST(stalker_portal_addresses) {
    stalker::Portal p = stalker::portalFor("http://portal.example.com:8080/c/");
    CHECK(p.ok && p.origin == "http://portal.example.com:8080");
    CHECK(!p.endpoints.empty() && p.endpoints[0] == "http://portal.example.com:8080/portal.php");
    CHECK(p.referer == "http://portal.example.com:8080/c/");
    bool hasLoad = false;
    for (const auto &e: p.endpoints) {
        hasLoad |= e == "http://portal.example.com:8080/stalker_portal/server/load.php";
    }
    CHECK(hasLoad);
    stalker::Portal q = stalker::portalFor("portal.example.com/stalker_portal/c");
    CHECK(q.ok && q.origin == "http://portal.example.com");
    CHECK(q.endpoints[0] == "http://portal.example.com/stalker_portal/server/load.php");
    CHECK(q.referer == "http://portal.example.com/stalker_portal/c/");
    stalker::Portal r = stalker::portalFor("https://portal.example.com/stalker_portal/server/load.php");
    CHECK(r.ok && r.endpoints.size() == 1 && r.endpoints[0] == "https://portal.example.com/stalker_portal/server/load.php");
    CHECK(r.referer == "https://portal.example.com/stalker_portal/c/");
    stalker::Portal s = stalker::portalFor("portal.example.com:25461");
    CHECK(s.ok && s.endpoints[0] == "http://portal.example.com:25461/portal.php");
    CHECK(!stalker::portalFor("").ok);
    CHECK(!stalker::portalFor("ftp://portal.example.com").ok);
    CHECK(!stalker::portalFor("http://user:pass@portal.example.com/c/").ok);
    // request building
    std::string url = stalker::apiUrl("http://h.example.com/portal.php", "itv", "get_genres");
    CHECK_EQ(url, std::string("http://h.example.com/portal.php?type=itv&action=get_genres&JsHttpRequest=1-xml"));
    stalker::Identity id = stalker::identity("00:1A:79:AB:CD:EF");
    auto h = stalker::headers(id, "http://h.example.com/c/", "");
    CHECK(hasHeader(h, "Cookie: mac=00%3A1A%3A79%3AAB%3ACD%3AEF; stb_lang=en; timezone=UTC"));
    CHECK(hasHeader(h, "Referer: http://h.example.com/c/"));
    CHECK(hasHeader(h, std::string("X-User-Agent: ") + stalker::X_USER_AGENT));
    for (const auto &line: h) {
        CHECK(line.find("Authorization") == std::string::npos);   // none before the handshake
    }
    CHECK(hasHeader(stalker::headers(id, "r", "T0KEN"), "Authorization: Bearer T0KEN"));
    std::string params = stalker::profileParams(id, 1700000000);
    CHECK(params.find("&sn=" + id.serial) != std::string::npos && params.find("stb_type=MAG250") != std::string::npos);
    CHECK(params.find("00:1A") == std::string::npos);   // the MAC itself travels in the cookie only
}

TEST(stalker_envelope_token_and_profile) {
    std::string token, err;
    CHECK(stalker::parseToken(R"({"js":{"token":"ABCDEF0123","random":"x"},"text":"..."})", token, err));
    CHECK_EQ(token, std::string("ABCDEF0123"));
    CHECK(!stalker::parseToken(R"({"js":{"token":""}})", token, err));
    CHECK(!stalker::parseToken("<html><body>Not found</body></html>", token, err));
    json::Value js;
    CHECK(stalker::envelope("Authorization failed.", js) == stalker::Reply::AuthFailed);
    CHECK(stalker::envelope("<html>", js) == stalker::Reply::NotJson);
    CHECK(stalker::envelope(R"({"result":1})", js) == stalker::Reply::NoJs);
    CHECK(stalker::envelope(R"({"js":[]})", js) == stalker::Reply::Ok && js.isArray());

    CHECK(stalker::parseProfile(R"({"js":{"id":"123","name":"box","status":0}})").ok);
    stalker::ProfileReply blocked = stalker::parseProfile(R"({"js":{"id":null,"status":1,"block_msg":"Account blocked"}})");
    CHECK(blocked.blocked && !blocked.ok && blocked.message == "Account blocked");
    CHECK(stalker::parseProfile(R"({"js":{"id":"5","status":2}})").needsLogin);
    CHECK(stalker::parseProfile(R"({"js":{"id":"0","status":0,"msg":"MAC not registered"}})").blocked);
    CHECK(!stalker::parseProfile("garbage").ok);

    AccountInfo account;
    stalker::parseAccountInfo(R"({"js":{"mac":"x","phone":"","end_date":"2027-03-01 00:00:00"}})", account);
    CHECK_EQ(account.expiresAt, stalker::parseDateTime("2027-03-01 00:00:00"));
    CHECK(account.expiresAt > 0 && account.status == "Active");
    CHECK_EQ(stalker::parseDateTime("1970-01-02 00:00:00"), (int64_t) 86400);
    CHECK_EQ(stalker::parseDateTime("2021-05-01"), (int64_t) 1619827200);
    CHECK_EQ(stalker::parseDateTime("unlimited"), (int64_t) 0);
}

TEST(stalker_live_categories_and_channels) {
    std::vector<Category> cats;
    std::string err;
    CHECK(stalker::parseCategories(R"({"js":[{"id":"*","title":"All"},{"id":"10","title":"News"},)"
                                   R"({"id":11,"title":"Sports"},{"id":"10","title":"Dup"}]})", cats, err));
    CHECK_EQ(cats.size(), (size_t) 2);
    CHECK(cats[0].id == "10" && cats[0].name == "News" && cats[1].id == "11");
    std::vector<LiveChannel> ch;
    stalker::Page page;
    std::string body = R"({"js":{"total_items":3,"max_page_items":14,"data":[)"
                       R"({"id":"501","name":"News One","number":"7","cmd":"ffmpeg http://localhost/ch/501_","tv_genre_id":"10",)"
                       R"("logo":"/stalker_portal/misc/logos/320/501.png","xmltv_id":"news1","tv_archive":1},)"
                       R"({"id":"502","name":"Sport Two","cmd":"ffrt http://stream.example.com/live/502.ts","tv_genre_id":"11",)"
                       R"("logo":"http://img.example.com/502.png"},)"
                       R"({"id":"503","name":"No Command","cmd":""}]}})";
    CHECK(stalker::parseChannels(body, "http://portal.example.com", ch, &page, err));
    CHECK_EQ(ch.size(), (size_t) 2);
    CHECK(ch[0].id == "501" && ch[0].name == "News One" && ch[0].num == 7 && ch[0].categoryId == "10");
    CHECK(ch[0].icon == "http://portal.example.com/stalker_portal/misc/logos/320/501.png");
    CHECK(ch[0].epgId == "news1" && ch[0].archive && ch[0].cmd == "ffmpeg http://localhost/ch/501_");
    CHECK(ch[0].url.empty() && !ch[0].isPlaylist());   // played through create_link, never as a playlist URL
    CHECK(ch[1].icon == "http://img.example.com/502.png");
    CHECK_EQ(page.totalItems, 3);
    CHECK_EQ(page.perPage, 14);
    // a second page appends and skips ids it already has
    CHECK(stalker::parseChannels(R"({"js":{"data":[{"id":"501","name":"Again","cmd":"x"},{"id":"504","name":"Four","cmd":"y"}]}})",
                                 "", ch, nullptr, err));
    CHECK_EQ(ch.size(), (size_t) 3);
    CHECK(!stalker::parseChannels("Authorization failed.", "", ch, nullptr, err));
}

TEST(stalker_vod_and_series_mapping) {
    std::vector<stalker::VodEntry> entries;
    stalker::Page page;
    std::string err;
    std::string body = R"({"js":{"total_items":"40","max_page_items":"14","data":[)"
                       R"({"id":"9001","name":"Example Movie 2019","o_name":"Example","description":"A plot.",)"
                       R"("director":"Someone","actors":"A, B","year":"2019-05-12","rating_imdb":"7.4","time":"95",)"
                       R"("genres_str":"Drama","screenshot_uri":"/stalker_portal/screenshots/9001.jpg","category_id":"3",)"
                       R"("added":"2024-01-02 10:00:00","cmd":"ffmpeg http://media.example.com/vod/9001.mkv","is_series":"0"},)"
                       R"({"id":"9002","name":"Old Style Series","is_series":"1","series":[3,1,2,2],)"
                       R"("cmd":"eyJ0eXBlIjoic2VyaWVzIn0=","category_id":"4"}]}})";
    CHECK(stalker::parseVodPage(body, "http://portal.example.com", entries, page, err));
    CHECK_EQ(entries.size(), (size_t) 2);
    CHECK(page.totalItems == 40 && page.perPage == 14 && page.count == 2);
    Movie m = stalker::toMovie(entries[0]);
    CHECK(m.streamId == "9001" && m.name == "Example Movie 2019" && m.title == "Example Movie" && m.year == 2019);
    CHECK(m.icon == "http://portal.example.com/stalker_portal/screenshots/9001.jpg");
    CHECK(m.extension == "mkv" && m.categoryId == "3" && m.rating > 7.3f && m.rating < 7.5f && m.added > 0);
    MovieInfo info = stalker::toMovieInfo(entries[0]);
    CHECK(info.plot == "A plot." && info.director == "Someone" && info.cast == "A, B" && info.durationSeconds == 95 * 60);
    CHECK(entries[1].isSeries && entries[1].episodes.size() == 4);
    Series s = stalker::toSeries(entries[1], "v:9002");
    CHECK(s.seriesId == "v:9002" && s.title == "Old Style Series" && s.categoryId == "4");
    SeriesInfo si = stalker::seriesFromEntry(entries[1], "v:9002");
    CHECK_EQ(si.seasons.size(), (size_t) 1);
    CHECK_EQ(si.seasons[0].episodes.size(), (size_t) 3);   // sorted, duplicates removed
    const Episode &e2 = si.seasons[0].episodes[1];
    CHECK(e2.number == 2 && e2.linkSeries == 2 && e2.cmd == "eyJ0eXBlIjoic2VyaWVzIn0=" && e2.id == "v:9002:2");

    // series module: seasons, then episodes
    std::vector<stalker::SeasonEntry> seasons;
    CHECK(stalker::parseSeasons(R"({"js":{"data":[{"id":"77:2","name":"Season 2","series":[]},)"
                                R"({"id":"77:1","name":"Season 1","cmd":"c1","series":[1,2]},{"id":"s3","name":"Sezon 3"}]}})",
                                seasons, page, err));
    CHECK(seasons.size() == 3 && seasons[0].number == 2 && seasons[1].number == 1 && seasons[2].number == 3);
    CHECK(seasons[1].episodes.size() == 2 && seasons[1].cmd == "c1");
    std::vector<Episode> eps;
    CHECK(stalker::parseEpisodes(R"({"js":{"data":[{"id":"e1","name":"Pilot","series_number":"1","cmd":"/media/e1.mpg",)"
                                 R"("time":"42"},{"id":"e2","series_number":2,"cmd":"ffmpeg http://media.example.com/e2.mp4"},)"
                                 R"({"id":"e3","name":"no cmd"}]}})", "http://p.example.com", 2, eps, page, err));
    CHECK_EQ(eps.size(), (size_t) 2);
    CHECK(eps[0].season == 2 && eps[0].number == 1 && eps[0].title == "Pilot" && eps[0].durationSeconds == 42 * 60);
    CHECK(eps[1].linkSeries == 2 && eps[1].extension == "mp4");
}

TEST(stalker_links_and_commands) {
    std::string url, err;
    CHECK(stalker::parseLink(R"({"js":{"id":"1","cmd":"ffmpeg http://s.example.com/play/live.php?mac=00:1A:79:AB:CD:EF&stream=501&play_token=Zz9","error":""}})",
                             url, err));
    CHECK_EQ(url, std::string("http://s.example.com/play/live.php?mac=00:1A:79:AB:CD:EF&stream=501&play_token=Zz9"));
    CHECK(!stalker::parseLink(R"({"js":{"cmd":"","error":"nothing_to_play"}})", url, err));
    CHECK_EQ(err, std::string("nothing_to_play"));
    CHECK_EQ(stalker::linkErrorKey("nothing_to_play"), std::string("stalker.link.nothing"));
    CHECK_EQ(stalker::linkErrorKey("limit"), std::string("stalker.link.limit"));
    CHECK_EQ(stalker::linkErrorKey("link_fault"), std::string("stalker.link.failed"));
    CHECK(!stalker::parseLink("<html>", url, err));
    CHECK_EQ(stalker::urlOfCmd("ffmpeg http://a.example.com/x.ts"), std::string("http://a.example.com/x.ts"));
    CHECK_EQ(stalker::urlOfCmd("auto https://a.example.com/x.m3u8"), std::string("https://a.example.com/x.m3u8"));
    CHECK_EQ(stalker::urlOfCmd("http://a.example.com/x.ts"), std::string("http://a.example.com/x.ts"));
    CHECK_EQ(stalker::urlOfCmd("/media/123.mpg"), std::string(""));
    CHECK(!stalker::cmdHasUrl("ffmpeg http://localhost/ch/501_"));
    CHECK(stalker::cmdHasUrl("ffrt http://stream.example.com/live/502.ts"));
    CHECK_EQ(stalker::resolveImage("http://p.example.com", "//cdn.example.com/x.png"), std::string(""));
    CHECK_EQ(stalker::resolveImage("http://p.example.com", "relative.png"), std::string(""));
}

TEST(stalker_saved_copy_round_trip) {
    std::vector<Category> cats = {{"10", "News \"Daily\"", ""}, {"11", "Spor", ""}};
    std::vector<Category> catsBack;
    std::string err;
    CHECK(stalker::decodeCategories(stalker::encodeCategories(cats), catsBack, err));
    CHECK(catsBack.size() == 2 && catsBack[0].name == "News \"Daily\"" && catsBack[1].id == "11");

    LiveChannel c;
    c.id = "501";
    c.name = "Haber \xC3\x87" "ok";
    c.categoryId = "10";
    c.icon = "http://img.example.com/1.png";
    c.num = 7;
    c.archive = true;
    c.cmd = "ffmpeg http://localhost/ch/501_";
    std::vector<LiveChannel> chBack;
    CHECK(stalker::decodeChannels(stalker::encodeChannels({c}), chBack, err));
    CHECK(chBack.size() == 1 && chBack[0].name == c.name && chBack[0].cmd == c.cmd && chBack[0].num == 7 && chBack[0].archive);

    stalker::VodEntry e;
    e.id = "v:9002";
    e.name = "Series";
    e.categoryId = "4";
    e.cmd = "abc";
    e.plot = "Line\nbreak";
    e.rating = 6.5f;
    e.minutes = 30;
    e.added = 1700000000;
    e.isSeries = true;
    e.episodes = {1, 2, 10};
    std::vector<stalker::VodEntry> back;
    CHECK(stalker::decodeEntries(stalker::encodeEntries({e}), back, err));
    CHECK(back.size() == 1 && back[0].id == "v:9002" && back[0].plot == "Line\nbreak" && back[0].isSeries);
    CHECK(back[0].episodes == std::vector<int>({1, 2, 10}) && back[0].minutes == 30 && back[0].added == 1700000000);
    CHECK(back[0].rating > 6.4f && back[0].rating < 6.6f);
    CHECK(!stalker::decodeEntries("not json", back, err));
}

TEST(stalker_profile_storage_and_migration) {
    std::string dir = tempDir("profiles_stalker");
    // an existing v0.3.0 file: one Xtream and one M3U source
    std::string v2 = R"({"version":2,"activeProfileId":"p1","profiles":[)"
                     R"({"id":"p1","sourceType":"xtream","name":"X","server":"http://x.example.com:8080","username":"xu","password":"xp","createdAt":5},)"
                     R"({"id":"p2","sourceType":"m3u","name":"M","playlistUrl":"http://m.example.com/a.m3u","userAgent":"","playlistChannels":12}]})";
    CHECK(fs::writeFileAtomic(fs::join(dir, "profiles.json"), v2));
    ProfileStore s(dir);
    CHECK(s.load());
    CHECK_EQ(s.profiles().size(), (size_t) 2);
    Profile k;
    k.type = SourceType::Stalker;
    k.name = "Portal";
    k.server = "http://portal.example.com:8080/c/";
    k.mac = "00:1A:79:AB:CD:EF";
    std::string kid = s.upsert(k, 100);
    CHECK(s.save());
    std::string saved;
    CHECK(fs::readFile(fs::join(dir, "profiles.json"), saved, 1 << 20));
    // still schema 2 (additive), the existing sources unchanged
    CHECK(saved.find("\"version\": 2") != std::string::npos || saved.find("\"version\":2") != std::string::npos);
    CHECK(saved.find("\"sourceType\": \"stalker\"") != std::string::npos || saved.find("\"sourceType\":\"stalker\"") != std::string::npos);
    CHECK(!fs::exists(fs::join(dir, "profiles.v1.json")));   // only a version 1 file is copied
    ProfileStore t(dir);
    CHECK(t.load());
    CHECK_EQ(t.profiles().size(), (size_t) 3);
    const Profile *x = t.find("p1");
    CHECK(x && x->type == SourceType::Xtream && x->username == "xu" && x->password == "xp" && x->createdAt == 5 && x->mac.empty());
    const Profile *m = t.find("p2");
    CHECK(m && m->type == SourceType::M3u && m->playlistChannels == 12);
    const Profile *p = t.find(kid);
    CHECK(p && p->isStalker() && p->server == k.server && p->mac == k.mac && p->hasVod() && !p->supportsDownloads());
    CHECK(x->hasVod() && x->supportsDownloads() && !m->hasVod() && !m->supportsDownloads());
    // idempotent
    CHECK(t.save());
    std::string again;
    CHECK(fs::readFile(fs::join(dir, "profiles.json"), again, 1 << 20));
    CHECK(again == saved);
    // an older build (v0.3.0 reader) skips the Stalker entry instead of rejecting the file: simulate with an
    // unknown type and check the known entries survive
    std::string future = R"({"version":2,"activeProfileId":"p9","profiles":[)"
                         R"({"id":"p1","sourceType":"xtream","name":"X","server":"http://x.example.com","username":"u","password":"p"},)"
                         R"({"id":"p9","sourceType":"something-new","name":"N","server":"http://n.example.com"}]})";
    ProfileStore u(dir);
    CHECK(u.deserialize(future, nullptr));
    CHECK_EQ(u.profiles().size(), (size_t) 1);
    CHECK_EQ(u.activeId(), std::string("p1"));
    // a Stalker entry without its MAC is incomplete and skipped
    ProfileStore w(dir);
    CHECK(w.deserialize(R"({"version":2,"profiles":[{"id":"p3","sourceType":"stalker","name":"S","server":"http://s.example.com"}]})", nullptr));
    CHECK(w.profiles().empty());
}

TEST(stalker_values_never_reach_logs) {
    redact::addMac("00:1A:79:12:34:56");
    for (const char *line: {"cookie mac=00:1A:79:12:34:56;", "mac 00-1a-79-12-34-56 x", "id 001A79123456",
                            "q=00%3A1A%3A79%3A12%3A34%3A56", "lower 00:1a:79:12:34:56"}) {
        std::string out = redact::apply(line);
        CHECK(out.find("12:34:56") == std::string::npos && out.find("123456") == std::string::npos
              && out.find("12-34-56") == std::string::npos && out.find("12%3A34") == std::string::npos
              && out.find("12:34:56") == std::string::npos && out.find("12:34:56") == std::string::npos);
    }
    // play links: MAC, play token and stream token in the query
    std::string link = redact::apply("http://s.example.com/play/live.php?mac=AA:BB:CC:DD:EE:FF&stream=501&play_token=Zz9Yy8&sn=ABCDEF1234567");
    CHECK(link.find("AA:BB") == std::string::npos && link.find("Zz9Yy8") == std::string::npos);
    CHECK(link.find("ABCDEF1234567") == std::string::npos && link.find("stream=501") != std::string::npos);
    // device ids and tokens registered as secrets
    stalker::Identity id = stalker::identity("00:1A:79:12:34:56");
    redact::addSecret(id.deviceId);
    redact::addSecret("SESSIONTOKEN42");
    std::string l2 = redact::apply("device " + id.deviceId + " Authorization: Bearer SESSIONTOKEN42");
    CHECK(l2.find(id.deviceId) == std::string::npos && l2.find("SESSIONTOKEN42") == std::string::npos);
}

namespace {
    // what v0.3.0 does with a file it opens and saves again (src/storage/profile_store.cpp at 590c3cc): entries with
    // a source type other than xtream / m3u are skipped, unknown top-level keys are not written back
    std::string savedByV030(const std::string &text, const std::string &addXtreamId = "") {
        json::Value root;
        json::parse(text, root);
        json::Value out = json::Value::makeObject();
        out.set("version", json::Value::makeInt(2));
        out.set("activeProfileId", json::Value::makeString(root["activeProfileId"].asString()));
        json::Value list = json::Value::makeArray();
        for (const auto &o: root["profiles"].items()) {
            std::string t = o["sourceType"].asString();
            if (t == "xtream" || t == "m3u") {
                list.push(o);
            }
        }
        if (!addXtreamId.empty()) {
            json::Value o = json::Value::makeObject();
            o.set("id", json::Value::makeString(addXtreamId));
            o.set("sourceType", json::Value::makeString("xtream"));
            o.set("name", json::Value::makeString("Added in v0.3.0"));
            o.set("server", json::Value::makeString("http://new.example.com"));
            o.set("username", json::Value::makeString("nu"));
            o.set("password", json::Value::makeString("np"));
            list.push(o);
        }
        out.set("profiles", std::move(list));
        return json::write(out);
    }
}

TEST(stalker_profiles_survive_a_downgrade_round_trip) {
    std::string dir = tempDir("profiles_downgrade");
    fs::removeFile(fs::join(dir, "profiles.stalker.json"));
    ProfileStore s(dir);
    Profile x;
    x.type = SourceType::Xtream;
    x.name = "X";
    x.server = "http://x.example.com";
    x.username = "u";
    x.password = "p";
    Profile m;
    m.type = SourceType::M3u;
    m.name = "M";
    m.playlistUrl = "http://m.example.com/a.m3u";
    Profile k;
    k.type = SourceType::Stalker;
    k.name = "Portal";
    k.server = "http://portal.example.com/c/";
    k.mac = "00:1A:79:AA:BB:CC";
    std::string xid = s.upsert(x, 1);
    std::string mid = s.upsert(m, 2);
    std::string kid = s.upsert(k, 3);
    s.setActive(kid);
    CHECK(s.save());
    CHECK(fs::exists(s.sidecarPath()));
    std::string written;
    CHECK(fs::readFile(s.path(), written, 1 << 20));

    // 1. downgrade: v0.3.0 signs in once (setLastStatus + save) and rewrites profiles.json without the portal
    std::string old = savedByV030(written);
    CHECK(old.find("stalker") == std::string::npos);
    CHECK(fs::writeFileAtomic(s.path(), old));
    // 2. upgrade again: the portal is back, nothing else changed
    ProfileStore t(dir);
    std::string warning;
    CHECK(t.load(&warning));
    CHECK_EQ(t.profiles().size(), (size_t) 3);
    const Profile *back = t.find(kid);
    CHECK(back && back->isStalker() && back->mac == k.mac && back->server == k.server && back->name == "Portal");
    CHECK(t.find(xid) && t.find(xid)->password == "p" && t.find(mid) && t.find(mid)->playlistUrl == m.playlistUrl);
    CHECK(warning.find("restored") != std::string::npos);
    // 3. v0.3.0 also added a source and reused the portal's id for it: both are kept, the portal gets a new id
    CHECK(fs::writeFileAtomic(s.path(), savedByV030(written, kid)));
    ProfileStore u(dir);
    CHECK(u.load());
    CHECK_EQ(u.profiles().size(), (size_t) 4);
    CHECK(u.find(kid) && u.find(kid)->type == SourceType::Xtream && u.find(kid)->name == "Added in v0.3.0");
    int portals = 0;
    for (const auto &p: u.profiles()) {
        portals += p.isStalker() && p.mac == k.mac && p.id != kid;
    }
    CHECK_EQ(portals, 1);
    // 4. a file written by this build is authoritative: a portal deleted here never comes back
    ProfileStore v(dir);
    CHECK(v.load());
    for (const auto &p: std::vector<Profile>(v.profiles())) {
        if (p.isStalker()) {
            v.remove(p.id);
        }
    }
    CHECK(v.save());
    CHECK(!fs::exists(v.sidecarPath()));   // no portal left: no sidecar
    ProfileStore w(dir);
    CHECK(w.load());
    for (const auto &p: w.profiles()) {
        CHECK(!p.isStalker());
    }
    // 5. an untouched v0.3.0 installation (no sidecar, no marker) loads exactly as before
    std::string legacy = tempDir("profiles_legacy");
    fs::removeFile(fs::join(legacy, "profiles.stalker.json"));
    std::string v030 = R"({"version":2,"activeProfileId":"p1","profiles":[{"id":"p1","sourceType":"xtream","name":"A",)"
                       R"("server":"http://a.example.com","username":"u","password":"p","createdAt":1,"lastUsedAt":2,"lastStatus":"Connected"}]})";
    CHECK(fs::writeFileAtomic(fs::join(legacy, "profiles.json"), v030));
    ProfileStore l(legacy);
    std::string lw;
    CHECK(l.load(&lw) && lw.empty() && l.profiles().size() == 1);
    // a damaged sidecar never breaks loading
    CHECK(fs::writeFileAtomic(fs::join(legacy, "profiles.stalker.json"), "{not json"));
    ProfileStore l2(legacy);
    CHECK(l2.load() && l2.profiles().size() == 1);
}

// found by fuzzing: numbers far outside int64 / int (portal or provider text) are clamped, never undefined
TEST(stalker_and_json_huge_numbers_are_clamped) {
    json::Value v;
    CHECK(json::parse(R"({"a":333333333333333333333333333333314,"b":-1e400,"c":1e400,"d":"12.9","e":"nan","f":9223372036854775807})", v));
    CHECK_EQ(v["a"].asInt(0), INT64_MAX);
    CHECK_EQ(v["b"].asInt(0), INT64_MIN);
    CHECK_EQ(v["c"].asInt(0), INT64_MAX);
    CHECK_EQ(v["d"].asInt(0), (int64_t) 12);
    CHECK_EQ(v["e"].asInt(7), (int64_t) 7);
    CHECK_EQ(v["f"].asInt(0), INT64_MAX);
    std::vector<stalker::VodEntry> entries;
    stalker::Page page;
    std::string err;
    CHECK(stalker::parseVodPage(R"({"js":{"total_items":1e30,"max_page_items":-5e40,"data":[{"id":"1","time":"99999999999",)"
                                R"("series":[1e20,-3,2,4000000000]}]}})", "", entries, page, err));
    CHECK(page.totalItems == 10000000 && page.perPage == 0);
    CHECK(entries.size() == 1 && entries[0].minutes == 100000);
    CHECK((entries[0].episodes == std::vector<int>{1000000, 2, 1000000}));
    CHECK(stalker::toMovieInfo(entries[0]).durationSeconds == 6000000);
    std::vector<Episode> eps;
    CHECK(stalker::parseEpisodes(R"({"js":{"data":[{"id":"e","cmd":"c","series_number":1e99,"time":1e99}]}})", "", 1, eps,
                                 page, err));
    CHECK(eps.size() == 1 && eps[0].number == 1000000 && eps[0].durationSeconds == 6000000);
    // a hostile episode list stays bounded
    std::string many = R"({"js":{"data":[{"id":"9","is_series":1,"series":[)";
    for (int i = 1; i <= 20000; i++) {
        many += std::to_string(i) + (i < 20000 ? "," : "");
    }
    many += "]}]}}";
    entries.clear();
    CHECK(stalker::parseVodPage(many, "", entries, page, err) && entries[0].episodes.size() == 5000);
}
