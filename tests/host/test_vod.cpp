// Movies / Series data layer: parsers (fixtures shaped like the real provider responses, sanitized), title
// helpers, catalogs, progress rules and Continue Watching.

#include <cstdlib>

#include "check.h"
#include "../../src/iptv/catalog.h"
#include "../../src/iptv/xtream.h"
#include "../../src/platform/fs.h"
#include "../../src/storage/library_store.h"

using namespace iptv;

namespace {
    // get_vod_streams entries as the provider sends them (ids/ratings numeric, nulls, https+http posters)
    const char *VOD_STREAMS = R"([
      {"num":1,"name":"1 Kezban 1 Mahmut Adana Yollarinda 2016","stream_type":"movie","stream_id":61061,
       "stream_icon":"https://img.example/images/vod/a.jpg","rating":6.2,"rating_5based":3.1,"added":"1658241501",
       "category_id":"83","container_extension":"mkv","custom_sid":null,"direct_source":""},
      {"num":2,"name":"975 2021","stream_type":"movie","stream_id":96825,"stream_icon":"http://img.example/b.jpg",
       "rating":5,"added":"1791236469","category_id":"98","container_extension":"mkv","custom_sid":null,"direct_source":""},
      {"num":3,"name":"Blade Runner 2049","stream_type":"movie","stream_id":"80178","stream_icon":null,"rating":null,
       "added":"1700000000","category_id":"98","container_extension":"mp4"},
      {"num":4,"name":"no id here","stream_type":"movie","category_id":"98"},
      {"num":5,"name":"","stream_id":5,"category_id":"x","container_extension":"mkv","rating":"7.5"}
    ])";

    const char *SERIES_LIST = R"J([
      {"num":1,"name":"American Hostage","series_id":2292,"cover":"https://img.example/c.jpg",
       "plot":"-","cast":"Jon Hamm, Giovanni Ribisi","director":"Shawn Ryan","genre":"Suç",
       "releaseDate":"2026-09-20","last_modified":"1791235192","rating":"7","rating_5based":3.5,
       "backdrop_path":[],"youtube_trailer":"","episode_run_time":"59","category_id":"9"},
      {"num":2,"name":"Crossing Lines (2013)","series_id":678,"cover":"","plot":"A team of detectives.",
       "releaseDate":"","rating":"bogus","category_id":"9","backdrop_path":["https://img.example/bd.jpg"]},
      {"num":3,"name":"broken","category_id":"9"}
    ])J";

    const char *VOD_INFO = R"({"info":{"duration_secs":8700,"duration":"02:25:00","bitrate":1844,
       "movie_image":"https://img.example/m.jpg","rating":7.9,"tmdb_id":"969681","cover_big":"https://img.example/big.jpg",
       "video":{"index":0,"codec_name":"h264","width":1920,"height":800,"disposition":{"default":1}},
       "audio":{"index":2,"codec_name":"ac3","channels":2,"channel_layout":"stereo","tags":{"language":"eng","title":"İngilizce"}}},
       "movie_data":{"stream_id":100497,"name":"Orumcek Adam 7 2026","added":"1791236469","category_id":"93",
       "container_extension":"mkv","custom_sid":null,"direct_source":""}})";

    const char *SERIES_INFO = R"({"seasons":[
       {"air_date":"2013-09-22","episode_count":2,"id":1,"name":"1. Sezon","overview":"-","season_number":1,"cover":"https://img.example/s1.jpg"},
       {"season_number":2,"name":"2. Sezon","cover":""},
       {"season_number":9,"name":"Empty season"}],
     "info":{"name":"Crossing Lines","cover":"https://img.example/c.jpg","plot":"-","cast":"William Fichtner",
       "director":"Edward Allen Bernero","genre":"Drama","releaseDate":"2013-06-23","rating":"7","episode_run_time":"45",
       "category_id":"9","backdrop_path":[]},
     "episodes":{
       "2":[{"id":"701","episode_num":1,"title":"Crossing Lines S02-E01","container_extension":"mkv","season":2,
             "info":{"duration_secs":2580,"movie_image":"https://img.example/e.jpg","rating":0}}],
       "1":[{"id":"602","episode_num":2,"title":"Crossing Lines S01E02 - The Hunter","container_extension":"mkv","season":1,
             "info":{"duration":"00:44:10","video":{"width":1920,"height":1080,"codec_name":"h264"}}},
            {"id":"601","episode_num":1,"title":"Crossing Lines S01-E01","container_extension":"mp4","season":1,"info":[]},
            {"episode_num":3,"title":"no id"}]}})";
}

TEST(vod_title_year_and_cleaning) {
    std::string t;
    int y;
    xtream::splitTitleYear("Orumcek Adam 7 2026", t, y);
    CHECK(t == "Orumcek Adam 7" && y == 2026);
    xtream::splitTitleYear("975 2021", t, y);
    CHECK(t == "975" && y == 2021);
    xtream::splitTitleYear("Crossing Lines (2013)", t, y);
    CHECK(t == "Crossing Lines" && y == 2013);
    xtream::splitTitleYear("Movie - 1999", t, y);
    CHECK(t == "Movie" && y == 1999);
    xtream::splitTitleYear("Blade Runner 2049", t, y);   // 2049 is in range: provider style "<title> <year>"
    CHECK(t == "Blade Runner" && y == 2049);
    xtream::splitTitleYear("2012", t, y);
    CHECK(t == "2012" && y == 0);
    xtream::splitTitleYear("Apollo13 1995", t, y);
    CHECK(t == "Apollo13" && y == 1995);
    xtream::splitTitleYear("Room 1408", t, y);           // not a year: stays in the title
    CHECK(t == "Room 1408" && y == 0);
    xtream::splitTitleYear("Fargo 1800", t, y);
    CHECK(t == "Fargo 1800" && y == 0);
    xtream::splitTitleYear("", t, y);
    CHECK(t.empty() && y == 0);

    CHECK(xtream::cleanText(" - ").empty());
    CHECK(xtream::cleanText("N/A").empty());
    CHECK(xtream::cleanText(" Drama ") == "Drama");
    CHECK(xtream::cleanEpisodeTitle("American Hostage S01-E01", "American Hostage").empty());
    CHECK(xtream::cleanEpisodeTitle("Crossing Lines S01E02 - The Hunter", "Crossing Lines") == "The Hunter");
    // the provider drops punctuation from episode titles: "Handmaids" for "Handmaid's"
    CHECK(xtream::cleanEpisodeTitle("The Handmaids Tale S01-E01", "The Handmaid's Tale").empty());
    CHECK(xtream::cleanEpisodeTitle("Trust Me - The False Prophet S01-E01", "Trust Me: The False Prophet").empty());
    CHECK(xtream::cleanEpisodeTitle("Showtime S01E01 Pilot", "Show") == "Showtime Pilot");   // whole words only
    CHECK(xtream::cleanEpisodeTitle("Dark S01E01 Secrets", "Dark") == "Secrets");
    CHECK(xtream::cleanEpisodeTitle("Pilot", "Show") == "Pilot");
    CHECK(xtream::cleanEpisodeTitle("Show s2 e10: Finale", "Show") == "Finale");
    CHECK(xtream::cleanEpisodeTitle("Mission S1 Starts", "X") == "Mission S1 Starts");   // no episode code
}

TEST(vod_streams_and_series_list_parse) {
    std::vector<Movie> movies;
    std::string err;
    CHECK(xtream::parseVodStreams(VOD_STREAMS, movies, err));
    CHECK_EQ(movies.size(), (size_t) 4);   // the entry without stream_id is skipped
    CHECK(movies[0].streamId == "61061" && movies[0].year == 2016);
    CHECK(movies[0].title == "1 Kezban 1 Mahmut Adana Yollarinda");
    CHECK(movies[0].extension == "mkv" && movies[0].categoryId == "83");
    CHECK(movies[0].rating > 6.1f && movies[0].rating < 6.3f && movies[0].added == 1658241501);
    CHECK(movies[1].icon == "http://img.example/b.jpg" && movies[1].rating == 5.0f);
    CHECK(movies[2].streamId == "80178" && movies[2].icon.empty() && movies[2].rating == 0 && movies[2].extension == "mp4");
    CHECK(movies[3].name == "Movie 5" && movies[3].rating > 7.4f);   // empty name, rating as a string
    CHECK(!xtream::parseVodStreams("{\"user_info\":{}}", movies, err));
    CHECK(!xtream::parseVodStreams("<html>", movies, err));
    CHECK(xtream::parseVodStreams("[]", movies, err) && movies.empty());

    std::vector<Series> series;
    CHECK(xtream::parseSeriesList(SERIES_LIST, series, err));
    CHECK_EQ(series.size(), (size_t) 2);
    CHECK(series[0].seriesId == "2292" && series[0].plot.empty() && series[0].genre == "Suç");
    CHECK(series[0].year == 2026 && series[0].rating == 7.0f && series[0].runtimeMinutes == 59);
    CHECK(series[0].cast == "Jon Hamm, Giovanni Ribisi" && series[0].director == "Shawn Ryan");
    CHECK(series[1].title == "Crossing Lines" && series[1].year == 2013 && series[1].rating == 0);
    CHECK(series[1].plot == "A team of detectives." && series[1].cover.empty());
}

TEST(vod_info_parse) {
    MovieInfo info;
    std::string err;
    CHECK(xtream::parseVodInfo(VOD_INFO, info, err));
    CHECK(info.streamId == "100497" && info.durationSeconds == 8700 && info.tmdbId == "969681");
    CHECK(info.rating > 7.8f && info.coverBig == "https://img.example/big.jpg");
    CHECK(info.plot.empty() && info.genre.empty() && info.director.empty());   // this provider sends none
    CHECK(info.media.width == 1920 && info.media.height == 800 && info.media.videoCodec == "h264");
    CHECK(info.media.audioCodec == "ac3" && info.media.audioChannels == 2 && info.media.audioLanguage == "eng");
    CHECK(info.media.bitrateKbps == 1844);
    // standard Xtream fields are used when a panel sends them
    CHECK(xtream::parseVodInfo(R"({"info":{"plot":"A story.","genre":"Drama","director":"X","actors":"A, B",
        "releasedate":"2019-01-01","duration":"01:30:00","backdrop_path":["https://img.example/bd.jpg"]},"movie_data":{}})",
                               info, err));
    CHECK(info.plot == "A story." && info.cast == "A, B" && info.durationSeconds == 5400);
    CHECK(info.backdrop == "https://img.example/bd.jpg");
    // "info": [] (no details) is not an error
    CHECK(xtream::parseVodInfo(R"({"info":[],"movie_data":{"stream_id":"1"}})", info, err) && info.streamId == "1");
    CHECK(!xtream::parseVodInfo("not json", info, err));
    CHECK(!xtream::parseVodInfo("[]", info, err));
}

TEST(series_info_parse) {
    SeriesInfo si;
    std::string err;
    CHECK(xtream::parseSeriesInfo(SERIES_INFO, si, err));
    CHECK(si.series.name == "Crossing Lines" && si.series.plot.empty() && si.series.year == 2013);
    CHECK(si.series.genre == "Drama" && si.series.runtimeMinutes == 45);
    CHECK_EQ(si.seasons.size(), (size_t) 2);   // season 9 has no episodes
    const Season &s1 = si.seasons[0];
    CHECK(s1.number == 1 && s1.name == "1. Sezon" && s1.cover == "https://img.example/s1.jpg");
    CHECK_EQ(s1.episodes.size(), (size_t) 2);   // the entry without an id is skipped
    CHECK(s1.episodes[0].id == "601" && s1.episodes[0].number == 1 && s1.episodes[0].title.empty());
    CHECK(s1.episodes[0].extension == "mp4");
    CHECK(s1.episodes[1].title == "The Hunter" && s1.episodes[1].durationSeconds == 2650);
    CHECK(s1.episodes[1].media.height == 1080);
    CHECK(si.seasons[1].number == 2 && si.seasons[1].episodes[0].durationSeconds == 2580);
    // episodes as an array of arrays, and a flat array, also parse
    CHECK(xtream::parseSeriesInfo(R"({"info":{"name":"S"},"episodes":[[{"id":"1","season":1,"episode_num":1}],
        [{"id":"2","season":2,"episode_num":1}]]})", si, err) && si.seasons.size() == 2);
    CHECK(xtream::parseSeriesInfo(R"({"info":{"name":"S"},"episodes":[{"id":"1","season":1,"episode_num":2},
        {"id":"0","season":1,"episode_num":1}]})", si, err) && si.seasons[0].episodes[0].id == "0");
    CHECK(xtream::parseSeriesInfo(R"({"info":[],"episodes":[]})", si, err) && si.seasons.empty());
    CHECK(!xtream::parseSeriesInfo("[1,2]", si, err));
}

TEST(vod_catalog_indices_search_and_recent) {
    std::vector<Movie> movies;
    std::string err;
    xtream::parseVodStreams(VOD_STREAMS, movies, err);
    std::vector<Category> cats = {{"83", "Yerli", ""}, {"98", "Aksiyon", ""}, {"999", "Empty", ""}};
    MovieCatalog c;
    c.assign(cats, movies);
    CHECK_EQ(c.categories().size(), (size_t) 2);   // empty categories are hidden
    CHECK_EQ(c.inCategory("98").size(), (size_t) 2);
    CHECK(c.inCategory("nope").empty());
    CHECK(c.find("96825") && c.find("96825")->title == "975");
    CHECK(c.find("missing") == nullptr);
    std::vector<int> recent = c.recent(10);
    CHECK(c.items()[(size_t) recent[0]].streamId == "96825");   // newest `added` first
    CHECK_EQ(c.recent(2).size(), (size_t) 2);
    std::vector<int> found = c.search("blade");
    CHECK(found.size() == 1 && c.items()[(size_t) found[0]].streamId == "80178");
    CHECK(c.search("KEZBAN").size() == 1);
    CHECK(c.favorites({"80178", "61061", "gone"}).size() == 2);
    CHECK_EQ(c.all().size(), c.size());
}

TEST(progress_rules_and_continue_watching) {
    using progress::canResume;
    using progress::isWatched;
    CHECK(!canResume(20, 6000, false));       // too early to bother
    CHECK(canResume(600, 6000, false));
    CHECK(!canResume(5700, 6000, false));     // 95 %: watched
    CHECK(isWatched(5600, 6000) && !isWatched(5500, 6000) && !isWatched(100, 0));
    CHECK(!canResume(600, 6000, true));
    CHECK(progress::resumeFrom(600) == 595 && progress::resumeFrom(8) == 0);
    CHECK(progress::fraction(300, 600) == 0.5 && progress::fraction(9, 0) == 0 && progress::fraction(700, 600) == 1);

    LibraryStore lib("unused");
    lib.setProfile("p1");
    HistoryEntry live;
    live.type = ContentType::Live;
    live.id = "ch1";
    live.position = 999;   // never resumable
    lib.addHistory(live);
    HistoryEntry movie;
    movie.type = ContentType::Movie;
    movie.id = "m1";
    movie.name = "Movie";
    movie.position = 1200;
    movie.duration = 6000;
    movie.extension = "mkv";
    lib.updateProgress(movie);
    HistoryEntry ep1;
    ep1.type = ContentType::Series;
    ep1.id = "e1";
    ep1.seriesId = "s1";
    ep1.seriesName = "Show";
    ep1.season = 1;
    ep1.episode = 1;
    ep1.position = 300;
    ep1.duration = 2400;
    lib.updateProgress(ep1);
    HistoryEntry ep2 = ep1;
    ep2.id = "e2";
    ep2.episode = 2;
    ep2.position = 2300;   // finished: the series is not "continue watching" any more
    ep2.watched = true;
    lib.updateProgress(ep2);
    std::vector<const HistoryEntry *> cw = lib.continueWatching(10);
    CHECK(cw.size() == 1 && cw[0]->id == "m1");
    std::vector<const HistoryEntry *> recent = lib.recentlyWatched(10);
    CHECK(recent.size() == 3 && recent[0]->id == "e2" && recent[1]->id == "m1" && recent[2]->id == "ch1");
    // progress lookup / reset
    CHECK(lib.progressOf(ContentType::Movie, "m1") && lib.progressOf(ContentType::Movie, "m1")->position == 1200);
    CHECK(lib.progressOf(ContentType::Series, "nope") == nullptr);
    lib.resetProgress(ContentType::Movie, "m1");
    CHECK(lib.progressOf(ContentType::Movie, "m1")->position == 0);
    CHECK(lib.continueWatching(10).empty());

    // persistence round trip, and profiles are separate
    lib.updateProgress(movie);
    std::string text = lib.serializeHistory();
    LibraryStore again("unused");
    std::string err;
    CHECK(again.deserializeHistory(text, &err));
    again.setProfile("p1");
    const HistoryEntry *m = again.progressOf(ContentType::Movie, "m1");
    CHECK(m && m->position == 1200 && m->duration == 6000 && m->extension == "mkv" && !m->watched);
    const HistoryEntry *e = again.progressOf(ContentType::Series, "e2");
    CHECK(e && e->watched && e->seriesId == "s1" && e->seriesName == "Show" && e->season == 1 && e->episode == 2);
    const HistoryEntry *l = again.progressOf(ContentType::Live, "ch1");
    CHECK(l && l->position == 0);
    again.setProfile("p2");
    CHECK(again.continueWatching(10).empty());
    // a Checkpoint 1 history.json (no new keys) still loads
    CHECK(again.deserializeHistory(R"({"version":1,"profiles":{"p1":[{"type":"live","id":"5","name":"TRT 1",
        "icon":"","extra":"","watchedAt":1,"position":0,"duration":0}]}})", &err));
}

TEST(vod_real_provider_samples_if_available) {
    const char *dir = std::getenv("PS4IPTV_SAMPLES");
    if (dir == nullptr) {
        std::printf("  (skipped: PS4IPTV_SAMPLES not set)\n");
        return;
    }
    std::string body, err;
    if (fs::readFile(fs::join(dir, "get_vod_streams.json"), body, 64u << 20)) {
        std::vector<Movie> movies;
        CHECK(xtream::parseVodStreams(body, movies, err));
        int withYear = 0;
        for (const auto &m: movies) {
            withYear += m.year > 0;
        }
        std::printf("     real provider sample: %d movies, %d with a year in the name\n", (int) movies.size(), withYear);
        CHECK(movies.size() > 1000);
        std::string cbody;
        std::vector<Category> cats;
        if (fs::readFile(fs::join(dir, "get_vod_categories.json"), cbody, 1u << 20)
            && xtream::parseCategories(cbody, cats, err)) {
            MovieCatalog c;
            c.assign(cats, movies);
            std::printf("     %d movie categories with items\n", (int) c.categories().size());
            CHECK(!c.categories().empty());
        }
    }
    if (fs::readFile(fs::join(dir, "get_series.json"), body, 64u << 20)) {
        std::vector<Series> series;
        CHECK(xtream::parseSeriesList(body, series, err));
        std::printf("     real provider sample: %d series\n", (int) series.size());
        CHECK(series.size() > 100);
    }
    int infos = 0;
    for (const char *sub: {"vod_info", "series_info"}) {
        std::string d = fs::join(dir, sub);
        for (const auto &f: fs::listDir(d)) {
            if (!fs::readFile(fs::join(d, f.name), body, 16u << 20)) {
                continue;
            }
            infos++;
            if (std::string(sub) == "vod_info") {
                MovieInfo mi;
                CHECK(xtream::parseVodInfo(body, mi, err));
                CHECK(mi.durationSeconds > 0);
            } else {
                SeriesInfo si;
                CHECK(xtream::parseSeriesInfo(body, si, err));
                CHECK(!si.seasons.empty() && !si.seasons[0].episodes.empty());
                CHECK(!si.seasons[0].episodes[0].extension.empty());
            }
        }
    }
    std::printf("     %d real detail responses parsed\n", infos);
}

#include "../../src/core/format.h"

TEST(vod_display_formatting) {
    CHECK(fmt::clock(0) == "0:00" && fmt::clock(2533) == "42:13" && fmt::clock(3723) == "1:02:03");
    CHECK(fmt::clock(2533, true) == "0:42:13" && fmt::clock(-5) == "0:00");
    CHECK(fmt::clockPair(2533, 6920) == "0:42:13 / 1:55:20");
    CHECK(fmt::clockPair(65, 2400) == "1:05 / 40:00");
    CHECK(fmt::clockPair(65, 0) == "1:05");
    CHECK(fmt::duration(8700) == "2 h 25 min" && fmt::duration(2700) == "45 min" && fmt::duration(3600) == "1 h");
    CHECK(fmt::duration(0).empty() && fmt::duration(20) == "1 min");
    CHECK(fmt::remaining(1200, 3720) == "42 min left" && fmt::remaining(5000, 4000).empty());
    CHECK(fmt::episodeCode(1, 3) == "S01E03" && fmt::episodeCode(12, 104) == "S12E104");
    CHECK(fmt::rating(7.9f) == "\xE2\x98\x85 7.9" && fmt::rating(7.0f) == "\xE2\x98\x85 7" && fmt::rating(0).empty());
    CHECK(fmt::resolution(1920, 800) == "1080p" && fmt::resolution(1920, 1080) == "1080p");
    CHECK(fmt::resolution(1280, 720) == "720p" && fmt::resolution(720, 408) == "408p");
    CHECK(fmt::resolution(3840, 1600) == "4K" && fmt::resolution(0, 0).empty());
}
