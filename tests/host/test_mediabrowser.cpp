// Jellyfin / Emby media servers (iptv/mediabrowser.h): server addresses, the authorization headers, the servers'
// answers (public information, sign-in, libraries, list pages, details, episodes), stream / image addresses,
// progress reports, dates, containers, the catalogs' saved copies; the "jellyfin" source type and its profiles
// (kept in a sidecar too, so a build that does not know the type never loses them).

#include <string>
#include <vector>

#include "check.h"
#include "../../src/core/json.h"
#include "../../src/iptv/mediabrowser.h"
#include "../../src/iptv/xtream.h"
#include "../../src/platform/fs.h"
#include "../../src/storage/profile_store.h"

namespace {
    const std::string BASE = "http://192.168.1.10:8096";

    std::string tmpDir(const std::string &name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(fs::join(base ? base : ".", "mediaserver"), name);
        for (const auto &e: fs::listDir(d)) {
            fs::removeFile(fs::join(d, e.name));
        }
        fs::ensureDir(d);
        return d;
    }

    bool contains(const std::string &s, const std::string &part) {
        return s.find(part) != std::string::npos;
    }
}

TEST(mediabrowser_addresses_and_headers) {
    std::string err;
    CHECK_EQ(mediabrowser::serverBase("192.168.1.10:8096", err), BASE);
    CHECK_EQ(mediabrowser::serverBase("http://192.168.1.10:8096/", err), BASE);
    CHECK_EQ(mediabrowser::serverBase("http://192.168.1.10:8096/web/index.html#!/home.html", err), BASE);
    CHECK_EQ(mediabrowser::serverBase("http://192.168.1.10:8096/web", err), BASE);
    CHECK_EQ(mediabrowser::serverBase("https://media.example.com/jellyfin/web/#!/details?id=1", err),
             std::string("https://media.example.com/jellyfin"));
    CHECK_EQ(mediabrowser::serverBase("https://media.example.com/emby", err), std::string("https://media.example.com/emby"));
    CHECK(mediabrowser::serverBase("", err).empty() && !err.empty());
    CHECK(mediabrowser::serverBase("ftp://media.example.com", err).empty());
    // a pasted address with user information never keeps it
    CHECK_EQ(mediabrowser::serverBase("http://exampleuser:examplepass@192.168.1.10:8096", err), BASE);

    std::string dev = mediabrowser::deviceId("p7");
    CHECK(dev == mediabrowser::deviceId("p7") && dev != mediabrowser::deviceId("p8") && dev.size() == 19);
    std::vector<std::string> h = mediabrowser::headers(dev, "0.4.1", "");
    CHECK_EQ(h.size(), (size_t) 3);
    CHECK(!h.empty() && contains(h[0], "Authorization: MediaBrowser Client=\"V7 Player\"")
          && contains(h[0], "DeviceId=\"" + dev + "\"") && contains(h[0], "Version=\"0.4.1\"") && !contains(h[0], "Token"));
    CHECK(h.size() > 1 && contains(h[1], "X-Emby-Authorization: MediaBrowser"));
    std::vector<std::string> t = mediabrowser::headers(dev, "0.4.1", "exa\"mple\r\ntoken");
    CHECK_EQ(t.size(), (size_t) 4);
    CHECK(contains(t[0], "Token=\"exampletoken\""));   // quotes / line breaks never reach a header
    CHECK(t.size() == 4 && t[3] == "X-Emby-Token: exampletoken");

    json::Value body;
    CHECK(json::parse(mediabrowser::loginBody("exampleuser", "pa\"ss\\word"), body));
    CHECK(body["Username"].asString() == "exampleuser" && body["Pw"].asString() == "pa\"ss\\word");

    std::string url = mediabrowser::streamUrl(BASE, "exampletoken", "a1b2");
    CHECK_EQ(url, BASE + "/Videos/a1b2/stream?static=true&api_key=exampletoken&ApiKey=exampletoken");
    CHECK_EQ(mediabrowser::imageUrl(BASE, "a1b2", "Primary", "tag 1", 450),
             BASE + "/Items/a1b2/Images/Primary?tag=tag%201&maxHeight=450&quality=85");
    std::string items = mediabrowser::itemsPath("u1", "lib 1", true, 500, 250);
    CHECK(contains(items, "/Users/u1/Items?ParentId=lib%201&IncludeItemTypes=Series&Recursive=true")
          && contains(items, "&StartIndex=500&Limit=250"));
    CHECK_EQ(std::string(mediabrowser::reportPath(mediabrowser::Report::Start)), std::string("/Sessions/Playing"));
    CHECK_EQ(std::string(mediabrowser::reportPath(mediabrowser::Report::Stop)), std::string("/Sessions/Playing/Stopped"));
    json::Value rep;
    CHECK(json::parse(mediabrowser::reportBody("a1b2", "s1", 12.5, true), rep));
    CHECK(rep["ItemId"].asString() == "a1b2" && rep["PositionTicks"].asInt(0) == 125000000 && rep["IsPaused"].asBool()
          && rep["PlaySessionId"].asString() == "s1" && rep["PlayMethod"].asString() == "DirectStream");
}

TEST(mediabrowser_sign_in_answers) {
    mediabrowser::ServerInfo si;
    CHECK(mediabrowser::parsePublicInfo("{\"LocalAddress\":\"http://192.168.1.10:8096\",\"ServerName\":\"home\","
                                        "\"Version\":\"10.10.3\",\"ProductName\":\"Jellyfin Server\",\"Id\":\"abc\"}", si));
    CHECK(si.product == "Jellyfin Server" && si.version == "10.10.3" && si.name == "home");
    CHECK(mediabrowser::parsePublicInfo("{\"ServerName\":\"emby\",\"Version\":\"4.8.10.0\",\"Id\":\"def\"}", si));
    CHECK_EQ(si.product, std::string("Emby Server"));   // Emby sends no product name
    CHECK(!mediabrowser::parsePublicInfo("<html><body>router login</body></html>", si));
    CHECK(!mediabrowser::parsePublicInfo("{\"user_info\":{\"auth\":0}}", si));   // an Xtream panel
    mediabrowser::Login l;
    CHECK(mediabrowser::parseLogin("{\"User\":{\"Name\":\"exampleuser\",\"Id\":\"u1\"},\"AccessToken\":\"exampletoken\","
                                   "\"ServerId\":\"abc\"}", l));
    CHECK(l.token == "exampletoken" && l.userId == "u1" && l.userName == "exampleuser" && l.serverId == "abc");
    CHECK(!mediabrowser::parseLogin("{\"User\":{\"Id\":\"u1\"}}", l));
    CHECK(!mediabrowser::parseLogin("Invalid username or password.", l));
    std::vector<mediabrowser::Library> libs;
    CHECK(mediabrowser::parseViews("{\"Items\":[{\"Id\":\"l1\",\"Name\":\"Filmler\",\"CollectionType\":\"movies\"},"
                                   "{\"Id\":\"l2\",\"Name\":\"Diziler\",\"CollectionType\":\"tvshows\"},"
                                   "{\"Id\":\"l3\",\"Name\":\"Music\",\"CollectionType\":\"music\"},"
                                   "{\"Id\":\"l4\",\"Name\":\"Mixed\"},{\"Name\":\"no id\"}],\"TotalRecordCount\":5}", libs));
    CHECK_EQ(libs.size(), (size_t) 4);
    CHECK(libs.size() == 4 && libs[0].type == "movies" && libs[1].type == "tvshows" && libs[3].type.empty());
    CHECK(!mediabrowser::parseViews("not json", libs));
}

TEST(mediabrowser_lists_and_details) {
    std::vector<iptv::Movie> movies;
    mediabrowser::Page page;
    CHECK(mediabrowser::parseMovies(
            "{\"Items\":[{\"Name\":\"Örümcek Adam\",\"Id\":\"m1\",\"Type\":\"Movie\",\"ProductionYear\":2002,"
            "\"CommunityRating\":7.4,\"DateCreated\":\"2024-03-01T12:00:00.0000000Z\",\"Container\":\"mkv\","
            "\"ImageTags\":{\"Primary\":\"t1\"}},"
            "{\"Name\":\"Clip\",\"Id\":\"m2\",\"Type\":\"Movie\",\"Container\":\"mov,mp4,m4a,3gp,3g2,mj2\","
            "\"CommunityRating\":42},{\"Name\":\"A folder\",\"Id\":\"f1\",\"Type\":\"Folder\"}],"
            "\"TotalRecordCount\":1203,\"StartIndex\":0}", BASE, "l1", movies, page));
    CHECK(page.total == 1203 && page.items == 3);
    CHECK_EQ(movies.size(), (size_t) 2);
    if (movies.size() == 2) {
        CHECK(movies[0].streamId == "m1" && movies[0].title == "Örümcek Adam" && movies[0].categoryId == "l1");
        CHECK(movies[0].year == 2002 && movies[0].rating > 7.3f && movies[0].rating < 7.5f && movies[0].extension == "mkv");
        CHECK_EQ(movies[0].added, (int64_t) 1709294400);
        CHECK_EQ(movies[0].icon, BASE + "/Items/m1/Images/Primary?tag=t1&maxHeight=450&quality=85");
        CHECK(movies[1].icon.empty() && movies[1].extension == "mp4" && movies[1].rating == 0);   // no tag: no image
    }
    std::vector<iptv::Series> series;
    CHECK(mediabrowser::parseSeries("{\"Items\":[{\"Name\":\"Show\",\"Id\":\"s1\",\"Type\":\"Series\","
                                    "\"ProductionYear\":2019,\"ImageTags\":{\"Primary\":\"t2\"}}],\"TotalRecordCount\":1}",
                                    BASE, "l2", series, page));
    CHECK(series.size() == 1 && series[0].seriesId == "s1" && series[0].year == 2019 && !series[0].cover.empty());

    iptv::MovieInfo mi;
    CHECK(mediabrowser::parseMovieInfo(
            "{\"Id\":\"m1\",\"Overview\":\"A story.\",\"Genres\":[\"Action\",\"Adventure\"],\"PremiereDate\":"
            "\"2002-05-03T00:00:00.0000000Z\",\"CommunityRating\":7.4,\"RunTimeTicks\":72600000000,"
            "\"People\":[{\"Name\":\"Sam\",\"Type\":\"Director\"},{\"Name\":\"Tobey\",\"Type\":\"Actor\"},"
            "{\"Name\":\"Kirsten\",\"Type\":\"Actor\"},{\"Name\":\"Writer\",\"Type\":\"Writer\"}],"
            "\"ProviderIds\":{\"Tmdb\":\"557\"},\"ImageTags\":{\"Primary\":\"t1\"},\"BackdropImageTags\":[\"b1\"],"
            "\"UserData\":{\"PlaybackPositionTicks\":18000000000,\"Played\":false},"
            "\"MediaSources\":[{\"Bitrate\":8000000,\"MediaStreams\":[{\"Type\":\"Video\",\"Codec\":\"HEVC\","
            "\"Width\":1920,\"Height\":1080},{\"Type\":\"Audio\",\"Codec\":\"ac3\",\"Channels\":6,\"Language\":\"tur\"},"
            "{\"Type\":\"Audio\",\"Codec\":\"aac\",\"Channels\":2}]}]}", BASE, mi));
    CHECK(mi.plot == "A story." && mi.genre == "Action, Adventure" && mi.director == "Sam" && mi.cast == "Tobey, Kirsten");
    CHECK(mi.releaseDate == "2002-05-03" && mi.durationSeconds == 7260 && mi.tmdbId == "557");
    CHECK(mi.resumeSeconds > 1799.9 && mi.resumeSeconds < 1800.1);
    CHECK(mi.media.width == 1920 && mi.media.videoCodec == "hevc" && mi.media.audioCodec == "ac3"
          && mi.media.audioChannels == 6 && mi.media.audioLanguage == "tur" && mi.media.bitrateKbps == 8000);
    CHECK(contains(mi.backdrop, "/Items/m1/Images/Backdrop?tag=b1"));
    CHECK(!mediabrowser::parseMovieInfo("{\"Name\":\"no id\"}", BASE, mi));

    iptv::SeriesInfo si;
    CHECK(mediabrowser::parseEpisodes(
            "{\"Items\":[{\"Id\":\"e3\",\"Name\":\"Third\",\"IndexNumber\":3,\"ParentIndexNumber\":1,\"SeasonName\":\"Season 1\","
            "\"Container\":\"mkv\",\"RunTimeTicks\":26000000000,\"UserData\":{\"PlaybackPositionTicks\":6000000000}},"
            "{\"Id\":\"e1\",\"Name\":\"Pilot\",\"IndexNumber\":1,\"ParentIndexNumber\":1,\"SeasonName\":\"Season 1\","
            "\"ImageTags\":{\"Primary\":\"t3\"},\"Overview\":\"Begins.\"},"
            "{\"Id\":\"e9\",\"Name\":\"Missing\",\"IndexNumber\":2,\"ParentIndexNumber\":1,\"LocationType\":\"Virtual\"},"
            "{\"Id\":\"x1\",\"Name\":\"Special\",\"IndexNumber\":1,\"ParentIndexNumber\":0,\"SeasonName\":\"Specials\"},"
            "{\"Id\":\"e21\",\"Name\":\"Next season\",\"IndexNumber\":1,\"ParentIndexNumber\":2}]}", BASE, si));
    CHECK_EQ(si.seasons.size(), (size_t) 3);   // 0, 1, 2 (the virtual episode is not playable)
    if (si.seasons.size() == 3) {
        CHECK(si.seasons[0].number == 0 && si.seasons[0].name == "Specials");
        const iptv::Season &s1 = si.seasons[1];
        CHECK(s1.number == 1 && s1.episodes.size() == 2);
        if (s1.episodes.size() == 2) {
            CHECK(s1.episodes[0].id == "e1" && s1.episodes[0].plot == "Begins." && !s1.episodes[0].image.empty());
            CHECK(s1.episodes[1].id == "e3" && s1.episodes[1].durationSeconds == 2600 && s1.episodes[1].resumeSeconds > 599);
            CHECK(s1.episodes[0].extension == "mkv");   // none given: the player's default
        }
        CHECK(si.seasons[2].number == 2 && si.seasons[2].episodes.size() == 1);
    }
    iptv::Series d;
    d.seriesId = "s1";
    d.name = "Show";
    d.year = 2019;
    CHECK(mediabrowser::parseSeriesDetail("{\"Id\":\"s1\",\"Name\":\"Show\",\"Overview\":\"About.\",\"Genres\":[\"Drama\"],"
                                          "\"People\":[{\"Name\":\"A\",\"Type\":\"Actor\"}],\"ProductionYear\":2020,"
                                          "\"BackdropImageTags\":[\"b2\"]}", BASE, d));
    CHECK(d.plot == "About." && d.genre == "Drama" && d.cast == "A" && d.year == 2019 && !d.backdrop.empty());
}

TEST(mediabrowser_values_and_saved_copies) {
    CHECK_EQ(mediabrowser::parseDate("2024-03-01T12:00:00.0000000Z"), (int64_t) 1709294400);
    CHECK_EQ(mediabrowser::parseDate("2024-03-01T15:00:00+03:00"), (int64_t) 1709294400);
    CHECK_EQ(mediabrowser::parseDate("2024-03-01"), (int64_t) 1709251200);
    CHECK_EQ(mediabrowser::parseDate("0001-01-01T00:00:00.0000000Z"), (int64_t) 0);   // .NET's "no date"
    CHECK_EQ(mediabrowser::parseDate("yesterday"), (int64_t) 0);
    CHECK_EQ(mediabrowser::containerOf("MKV"), std::string("mkv"));
    CHECK_EQ(mediabrowser::containerOf("mov,mp4,m4a,3gp,3g2,mj2"), std::string("mp4"));
    CHECK_EQ(mediabrowser::containerOf("mpegts,ts"), std::string("mpegts"));
    CHECK_EQ(mediabrowser::containerOf(""), std::string("mkv"));

    std::vector<iptv::Category> cats = {{"l1", "Filmler", ""}};
    std::vector<iptv::Movie> movies(2);
    movies[0].streamId = "m1";
    movies[0].name = movies[0].title = "Örümcek Adam";
    movies[0].categoryId = "l1";
    movies[0].icon = BASE + "/Items/m1/Images/Primary?tag=t1";
    movies[0].extension = "mkv";
    movies[0].rating = 7.5f;
    movies[0].year = 2002;
    movies[0].added = 1709294400;
    movies[1].streamId = "m2";
    movies[1].name = movies[1].title = "Quote \"x\"";
    movies[1].categoryId = "l1";
    std::vector<iptv::Category> c2;
    std::vector<iptv::Movie> m2;
    CHECK(mediabrowser::decodeMovies(mediabrowser::encodeMovies(cats, movies), c2, m2));
    CHECK(c2.size() == 1 && c2[0].name == "Filmler" && m2.size() == 2);
    if (m2.size() == 2) {
        CHECK(m2[0].title == "Örümcek Adam" && m2[0].icon == movies[0].icon && m2[0].year == 2002
              && m2[0].added == 1709294400 && m2[0].rating > 7.4f);
        CHECK(m2[1].title == "Quote \"x\"" && m2[1].extension == "mkv");
    }
    std::vector<iptv::Series> series(1);
    series[0].seriesId = "s1";
    series[0].name = series[0].title = "Show";
    series[0].categoryId = "l2";
    std::vector<iptv::Series> s2;
    CHECK(mediabrowser::decodeSeries(mediabrowser::encodeSeries({{"l2", "Diziler", ""}}, series), c2, s2));
    CHECK(s2.size() == 1 && s2[0].seriesId == "s1" && c2.size() == 1);
    CHECK(!mediabrowser::decodeMovies("{broken", c2, m2));
    CHECK(!mediabrowser::decodeMovies("[]", c2, m2));   // no library: not a saved catalog
}

TEST(mediabrowser_profiles_survive_older_builds) {
    iptv::SourceType t;
    CHECK(iptv::sourceTypeFromKey("jellyfin", t) && t == iptv::SourceType::Jellyfin);
    CHECK_EQ(std::string(iptv::sourceTypeKey(iptv::SourceType::Jellyfin)), std::string("jellyfin"));
    iptv::Profile p;
    p.type = iptv::SourceType::Jellyfin;
    CHECK(p.isMediaServer() && p.hasVod() && !p.hasLive() && !p.supportsDownloads() && !p.isStalker());

    std::string dir = tmpDir("profiles");
    ProfileStore s(dir);
    iptv::Profile x;
    x.name = "Provider";
    x.server = "http://xtream.example.com:8080";
    x.username = "exampleuser";
    x.password = "examplepass";
    s.upsert(x, 100);
    iptv::Profile j;
    j.type = iptv::SourceType::Jellyfin;
    j.name = "Home server";
    j.server = BASE;
    j.username = "exampleuser";
    j.password = "";   // a user without a password
    std::string jid = s.upsert(j, 100);
    CHECK(s.save());
    CHECK(fs::exists(s.mediaSidecarPath()));
    ProfileStore again(dir);
    CHECK(again.load());
    const iptv::Profile *back = again.find(jid);
    CHECK(back != nullptr && back->isMediaServer() && back->server == BASE && back->username == "exampleuser"
          && back->password.empty());

    // an older build (v0.4.1) reads profiles.json, skips the "jellyfin" entry it does not know and saves the list
    // without it (and without the media marker): the next load of this build brings the server back
    std::string text;
    CHECK(fs::readFile(fs::join(dir, "profiles.json"), text, 1 << 20));
    json::Value root;
    CHECK(json::parse(text, root));
    json::Value older = json::Value::makeObject();
    older.set("version", json::Value::makeInt(2));
    older.set("activeProfileId", json::Value::makeString(jid));
    older.set("stalkerSidecar", json::Value::makeInt(1));   // v0.4.1 knows Stalker sources
    json::Value list = json::Value::makeArray();
    for (const auto &e: root["profiles"].items()) {
        if (e["sourceType"].asString() != "jellyfin") {
            list.push(e);
        }
    }
    older.set("profiles", std::move(list));
    CHECK(fs::writeFileAtomic(fs::join(dir, "profiles.json"), json::write(older)));
    ProfileStore restored(dir);
    std::string warning;
    CHECK(restored.load(&warning));
    CHECK(restored.find(jid) != nullptr && restored.profiles().size() == 2);
    CHECK(contains(warning, "media server(s) restored"));
    // written by this build: the marker is there, nothing to restore, nothing doubled
    CHECK(restored.save());
    ProfileStore fourth(dir);
    std::string w2;
    CHECK(fourth.load(&w2) && fourth.profiles().size() == 2 && !contains(w2, "media server"));
    // no media server left: no sidecar
    CHECK(fourth.remove(jid) && fourth.save());
    CHECK(!fs::exists(fourth.mediaSidecarPath()));
}
