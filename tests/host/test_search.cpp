// Search index: normalization (Turkish letters, apostrophes, punctuation, spaces), trailing-year aliases,
// ranking, fuzzy matching and performance at the real catalog scale (19,797 movies / 2,337 series / 573 live).

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/iptv/catalog.h"
#include "../../src/iptv/search_index.h"
#include "../../src/iptv/xtream.h"
#include "../../src/platform/fs.h"

using namespace iptv;

namespace {
    double msSince(std::chrono::steady_clock::time_point t0) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    // index over display names; returns the names in result order
    std::vector<std::string> top(const search::Index &index, const std::vector<std::string> &names,
                                 const std::string &query, size_t n = 5) {
        std::vector<std::string> out;
        for (const auto &h: index.find(query, n).hits) {
            out.push_back(names[(size_t) h.item]);
        }
        return out;
    }

    Movie movie(const std::string &id, const std::string &name) {
        Movie m;
        m.streamId = id;
        m.name = name;
        xtream::splitTitleYear(name, m.title, m.year);
        return m;
    }
}

TEST(search_normalization) {
    using search::normalize;
    CHECK(normalize("The Handmaid's Tale") == "the handmaids tale");
    CHECK(normalize("The Handmaid\xE2\x80\x99s Tale") == "the handmaids tale");          // ’
    CHECK(normalize("  How   I Met\tYour  Mother ") == "how i met your mother");
    CHECK(normalize("Spider-Man: No Way Home") == "spider man no way home");
    CHECK(normalize("S.W.A.T.") == "swat");
    CHECK(normalize("U.S. Marshals") == "us marshals");
    CHECK(normalize("Mr. Robot") == "mr robot");
    CHECK(normalize("Fast & Furious 7!") == "fast furious 7");
    CHECK(normalize("\xC5\x9E" "ehir") == "sehir");                                        // Şehir
    CHECK(normalize("\xC3\x96" "l\xC3\xBC" "m") == "olum");                                // Ölüm
    CHECK(normalize("B\xC3\xBC" "lb\xC3\xBC" "l\xC3\xBC \xC3\x96" "ld\xC3\xBC" "rmek")
          == "bulbulu oldurmek");                                                          // Bülbülü Öldürmek
    CHECK(normalize("\xC4\xB0stanbul \xC4\x9E" "\xC3\x9C\xC5\x9E\xC4\xB0\xC3\x96\xC3\x87 \xC4\xB1") ==
          "istanbul gusioc i");                                                            // İstanbul ĞÜŞİÖÇ ı
    CHECK(normalize("\xC3\xA7" "\xC4\x9F" "\xC4\xB1" "\xC3\xB6" "\xC5\x9F" "\xC3\xBC") == "cgiosu");   // çğıöşü
    CHECK(normalize("Bizans'\xC4\xB1n K\xC4\xB1yameti") == "bizansin kiyameti");
    CHECK(normalize("\xE2\x98\x85 Top \xC2\xB7 10 \xC2\xAB" "Best\xC2\xBB") == "top 10 best");    // symbols are spaces
    CHECK(normalize("\xD0\x91\xD1\x80\xD0\xB0\xD1\x82") == "\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82");   // Брат -> брат
    CHECK(normalize("").empty() && normalize(" - . ' ").empty());
    std::vector<std::string> t = search::tokens("the handmaids tale");
    CHECK(t.size() == 3 && t[1] == "handmaids");

    CHECK(search::editDistance("braking", "breaking", 2) == 1);
    CHECK(search::editDistance("dnue", "dune", 1) == 1);            // transposition
    CHECK(search::editDistance("abc", "xyz", 1) == 2);              // cut off
    CHECK(search::editDistance("matrx", "matrix", 1) == 1);
    CHECK(search::editDistance("brea", "breaking", 1, true) == 0);  // prefix of the word being typed
    CHECK(search::editDistance("brek", "breaking", 1, true) == 1);
}

TEST(search_turkish_punctuation_and_examples) {
    std::vector<std::string> names = {
            "\xC5\x9E" "ehir Ma\xC4\x9F" "aralar\xC4\xB1",          // 0 Şehir Mağaraları
            "\xC3\x96" "l\xC3\xBC" "m Vadisi",                      // 1 Ölüm Vadisi
            "B\xC3\xBC" "lb\xC3\xBC" "l\xC3\xBC \xC3\x96" "ld\xC3\xBC" "rmek",   // 2 Bülbülü Öldürmek
            "The Handmaid's Tale",                                  // 3
            "How I Met Your Mother",                                // 4
            "Mother's Day",                                         // 5
            "Breaking Bad",                                         // 6
            "Bad Boys",                                             // 7
            "Kanal \xC4\xB0stanbul",                                // 8 Kanal İstanbul
            "TR: TRT 1 HD",                                         // 9
            "TRT 10",                                               // 10
    };
    search::Index index;
    index.build(names);
    CHECK_EQ(index.size(), (int) names.size());
    CHECK(top(index, names, "sehir").size() == 1 && top(index, names, "sehir")[0] == names[0]);
    CHECK(top(index, names, "SEHIR")[0] == names[0]);
    CHECK(top(index, names, "\xC5\x9F" "ehir")[0] == names[0]);           // typed with the Turkish letter
    CHECK(top(index, names, "olum")[0] == names[1]);
    CHECK(top(index, names, "bulbulu oldurmek")[0] == names[2]);
    CHECK(top(index, names, "handmaids tale")[0] == names[3]);
    CHECK(top(index, names, "handmaid tale")[0] == names[3]);
    CHECK(top(index, names, "handmaid's tale")[0] == names[3]);
    CHECK(top(index, names, "the handmaids-tale")[0] == names[3]);
    CHECK(top(index, names, "how i met your mother")[0] == names[4]);
    CHECK(top(index, names, "how i met")[0] == names[4]);
    CHECK(top(index, names, "istanbul")[0] == names[8]);
    // Live TV: provider prefixes ("TR: ") are searchable without the prefix
    LiveCatalog live;
    std::vector<LiveChannel> channels;
    for (const char *n: {"TR: TRT 10", "TR: TRT 1", "TR: TRT Spor", "HD: beIN Sports 1", "|DE| ZDF HD"}) {
        LiveChannel ch;
        ch.streamId = std::to_string(channels.size());
        ch.name = n;
        channels.push_back(ch);
    }
    live.assign({}, channels);
    search::Result trt = live.searchRanked("trt 1", 10);
    CHECK(trt.total == 2 && live.channels()[(size_t) trt.hits[0].item].name == "TR: TRT 1");
    CHECK(trt.hits[0].score == search::EXACT);
    CHECK(live.channels()[(size_t) live.searchRanked("bein sports 1", 5).hits[0].item].name == "HD: beIN Sports 1");
    CHECK(live.channels()[(size_t) live.searchRanked("zdf", 5).hits[0].item].name == "|DE| ZDF HD");
    CHECK(xtream::channelNameWithoutPrefix("TR: TRT 1") == "TRT 1");
    CHECK(xtream::channelNameWithoutPrefix("|DE| ZDF") == "ZDF");
    CHECK(xtream::channelNameWithoutPrefix("TRT 1").empty());
    CHECK(xtream::channelNameWithoutPrefix("Movie: The Return").empty());   // a word, not a prefix
    // "mother": the word in both titles; the shorter title first is not required, both found
    CHECK(top(index, names, "mother").size() == 2);
    CHECK(index.find("zzzzqqqq", 10).hits.empty());
    CHECK(index.find("   ", 10).hits.empty());
    // display text is never changed by indexing
    CHECK(names[3] == "The Handmaid's Tale");
}

TEST(search_trailing_year_alias) {
    std::vector<Movie> movies = {movie("1", "Primate 2026"), movie("2", "Primate Planet 2019"),
                                 movie("3", "Room 1408"), movie("4", "2012"), movie("5", "Blade Runner 2049"),
                                 movie("6", "Assassin's Game - Maximillian - 2019")};
    MovieCatalog c;
    c.prepare({}, movies);
    auto first = [&c](const std::string &q) {
        auto r = c.searchRanked(q, 5);
        return r.hits.empty() ? std::string() : c.items()[(size_t) r.hits[0].item].streamId;
    };
    auto firstScore = [&c](const std::string &q) {
        auto r = c.searchRanked(q, 5);
        return r.hits.empty() ? 0 : r.hits[0].score;
    };
    CHECK(first("primate") == "1" && firstScore("primate") == search::EXACT);           // alias "Primate"
    CHECK(first("primate 2026") == "1" && firstScore("primate 2026") == search::EXACT);  // full provider name
    CHECK(c.searchRanked("primate", 5).total == 2);
    CHECK(first("room 1408") == "3" && first("room") == "3");     // not a year: part of the title
    CHECK(first("1408") == "3");
    CHECK(first("2012") == "4");                                   // a name that is only a year
    CHECK(first("blade runner") == "5" && first("blade runner 2049") == "5");
    CHECK(first("assassins game") == "6" && first("assassin's game maximillian") == "6");
    CHECK(c.diagnostics().indexed == 6);
}

TEST(search_ranking) {
    std::vector<std::string> names = {"Sand Dunes of Arrakis", "Dune: Part Two", "Dunkirk", "Dune",
                                      "The Dune Chronicles", "Dunes"};
    search::Index index;
    index.build(names);
    std::vector<std::string> r = top(index, names, "dune", 10);
    CHECK(r.size() >= 5);
    CHECK(r[0] == "Dune");                      // exact
    CHECK(r[1] == "Dune: Part Two");            // starts with the whole word
    CHECK(r[2] == "Dunes");                     // starts with it inside a word
    CHECK(r[3] == "The Dune Chronicles");       // whole word inside
    CHECK(r[4] == "Sand Dunes of Arrakis");     // word prefix inside
    // the exact title outranks longer titles with the same words
    std::vector<std::string> h = {"The Handmaid's Tale: Behind the Scenes", "A Tale of Handmaids",
                                  "The Handmaid's Tale"};
    search::Index hi;
    hi.build(h);
    CHECK(top(hi, h, "the handmaids tale")[0] == "The Handmaid's Tale");
    CHECK(top(hi, h, "handmaid tale")[0] == "The Handmaid's Tale");   // shortest of the in-order matches
    search::Result res = hi.find("handmaid tale", 10);
    CHECK(res.total == 3 && res.hits[0].score > res.hits[2].score);   // "A Tale of Handmaids": not in order
}

TEST(search_fuzzy) {
    std::vector<std::string> names = {"Breaking Bad", "Bad Boys", "Braveheart", "The Matrix", "Dune",
                                      "Better Call Saul", "Breaking Point", "Bad Breaking News"};
    search::Index index;
    index.build(names);
    std::vector<std::string> r = top(index, names, "braking bad", 5);
    CHECK(!r.empty() && r[0] == "Breaking Bad");
    CHECK(top(index, names, "breking bad")[0] == "Breaking Bad");
    CHECK(top(index, names, "dnue")[0] == "Dune");
    CHECK(top(index, names, "the matrx")[0] == "The Matrix");
    CHECK(top(index, names, "beter call sual")[0] == "Better Call Saul");
    // false positives stay out: every word must match and short words are never corrected
    CHECK(index.find("bad", 10).total == 3);
    CHECK(index.find("zxqv matrix", 10).hits.empty());
    CHECK(index.find("cat", 10).hits.empty());           // 3 letters: no typo correction
    search::Result fuzzy = index.find("braking bad", 10);
    for (const auto &h: fuzzy.hits) {
        CHECK(h.score <= search::FUZZY || names[(size_t) h.item] != "Breaking Bad");
    }
    // a typo is ranked below exact matches of the same query words
    CHECK(index.find("breaking bad", 10).hits[0].score > index.find("braking bad", 10).hits[0].score);
}

namespace {
    // deterministic, provider-like names (Turkish and English words, years, apostrophes, hyphens)
    std::vector<std::string> syntheticNames(size_t n, unsigned seed, bool years) {
        const char *words[] = {"the", "dark", "night", "\xC3\xB6l\xC3\xBCm", "\xC5\x9F" "ehir", "a\xC5\x9Fk", "game",
                               "kayıp", "love", "story", "war", "son", "day", "\xC4\xB0stanbul", "king", "queen",
                               "Handmaid's", "tale", "breaking", "bad", "mother", "father", "g\xC3\xBCne\xC5\x9F",
                               "deniz", "y\xC4\xB1ld\xC4\xB1z", "lost", "city", "spider-man", "blue", "red", "bir",
                               "zaman", "dune", "matrix", "\xC3\xA7" "ocuk", "do\xC4\x9F" "a", "last", "first", "home",
                               "road", "fire", "ice", "Kurtlar", "Vadisi", "aile", "s\xC4\xB1r", "ev", "yol",
                               "ate\xC5\x9F", "kalp", "gece", "g\xC3\xBCnd\xC3\xBCz", "su", "toprak", "hava",
                               "ruh", "rüya", "kanun"};
        const size_t W = sizeof(words) / sizeof(words[0]);
        uint32_t x = seed;
        auto next = [&x]() {
            x = x * 1664525u + 1013904223u;
            return x >> 8;
        };
        std::vector<std::string> out;
        out.reserve(n);
        for (size_t i = 0; i < n; i++) {
            std::string name;
            size_t len = 1 + next() % 4;
            for (size_t k = 0; k < len; k++) {
                name += (k ? " " : "") + std::string(words[next() % W]);
            }
            if (next() % 7 == 0) {
                name += " " + std::to_string(next() % 9 + 1);
            }
            if (years) {
                name += " " + std::to_string(1960 + next() % 67);
            }
            out.push_back(name);
        }
        return out;
    }
}

TEST(search_performance_at_real_scale) {
    struct Case {
        const char *what;
        size_t n;
        bool years;
    };
    const Case cases[] = {{"movies", 19797, true}, {"series", 2337, false}, {"live", 573, false}};
    const char *queries[] = {"d", "da", "dar", "dark", "dark night", "sehir", "olum", "handmaid tale",
                             "how i met", "spiderman", "braking bad", "kurtlar vadisi", "istanbul 2019",
                             "the", "zzzz"};
    const int Q = (int) (sizeof(queries) / sizeof(queries[0]));
    for (const Case &c: cases) {
        std::vector<std::string> names = syntheticNames(c.n, 12345u + (unsigned) c.n, c.years);
        std::vector<std::string> aliases(names.size());
        for (size_t i = 0; c.years && i < names.size(); i++) {
            int y;
            xtream::splitTitleYear(names[i], aliases[i], y);
        }
        auto t0 = std::chrono::steady_clock::now();
        search::Index index;
        index.build(names, aliases);
        double buildMs = msSince(t0);
        double total = 0;
        double worst = 0;
        int rounds = 3;
        for (int r = 0; r < rounds; r++) {
            for (int q = 0; q < Q; q++) {
                auto t1 = std::chrono::steady_clock::now();
                search::Result res = index.find(queries[q], 400);
                double ms = msSince(t1);
                total += ms;
                worst = std::max(worst, ms);
                CHECK(res.hits.size() <= 400);
            }
        }
        double avg = total / (rounds * Q);
        std::printf("     search %s: %zu items, index build %.1f ms, %.2f MB; query avg %.2f ms, worst %.2f ms\n",
                    c.what, c.n, buildMs, (double) index.memoryBytes() / (1024.0 * 1024.0), avg, worst);
        CHECK_EQ(index.size(), (int) c.n);
        // generous host bounds (the PS4 is roughly 5-8x slower): a query must stay far below a frame budget
        CHECK(buildMs < 3000);
        CHECK(avg < 40);
        CHECK(index.memoryBytes() < 32u * 1024 * 1024);
    }
}

TEST(search_real_provider_samples_if_available) {
    const char *dir = std::getenv("PS4IPTV_SAMPLES");
    if (dir == nullptr) {
        std::printf("  (skipped: PS4IPTV_SAMPLES not set)\n");
        return;
    }
    std::string body, err;
    std::vector<Movie> movies;
    if (!fs::readFile(fs::join(dir, "get_vod_streams.json"), body, 64u << 20)
        || !xtream::parseVodStreams(body, movies, err)) {
        std::printf("  (skipped: no movie sample)\n");
        return;
    }
    MovieCatalog c;
    auto t0 = std::chrono::steady_clock::now();
    c.assign({}, movies);
    c.buildSearch();
    double buildMs = msSince(t0);
    CHECK_EQ(c.diagnostics().indexed, (int) c.size());
    const char *queries[] = {"sehir", "olum", "ask", "dune", "handmaid", "kurtlar", "spiderman", "braking bad",
                             "a", "the", "2024"};
    double total = 0, worst = 0;
    int found = 0;
    for (const char *q: queries) {
        auto t1 = std::chrono::steady_clock::now();
        search::Result r = c.searchRanked(q, 400);
        double ms = msSince(t1);
        total += ms;
        worst = std::max(worst, ms);
        found += r.total > 0;
    }
    std::printf("     real movies: index %d entries built in %.1f ms (%.2f MB); %d/%d sample queries matched, "
                "avg %.2f ms, worst %.2f ms\n", c.diagnostics().indexed, buildMs,
                (double) c.diagnostics().indexBytes / (1024.0 * 1024.0), found,
                (int) (sizeof(queries) / sizeof(queries[0])), total / (double) (sizeof(queries) / sizeof(queries[0])),
                worst);
    // every movie is findable by its own title
    int missing = 0;
    for (size_t i = 0; i < c.size(); i += 97) {
        const Movie &m = c.items()[i];
        bool hit = false;
        for (const auto &h: c.searchRanked(m.title, 2000).hits) {
            hit = hit || h.item == (int) i;
        }
        missing += !hit;
    }
    CHECK_EQ(missing, 0);
}
