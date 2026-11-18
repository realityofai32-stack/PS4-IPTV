// The live player's programme guide (iptv/epg.h): the Xtream / Stalker short-guide answers, base64 texts, the
// programme on air and the next ones, the session cache (when a channel is asked again) and which sources are asked
// at all. Nothing may be made up: entries without a usable time or title are dropped, never repaired.

#include <string>
#include <vector>

#include "check.h"
#include "../../src/iptv/epg.h"
#include "../../src/platform/clock.h"

using epg::Cache;
using epg::Programme;

namespace {
    std::string b64(const std::string &in) {
        static const char *abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        size_t i = 0;
        for (; i + 2 < in.size(); i += 3) {
            unsigned v = ((unsigned char) in[i] << 16) | ((unsigned char) in[i + 1] << 8) | (unsigned char) in[i + 2];
            out += abc[(v >> 18) & 63];
            out += abc[(v >> 12) & 63];
            out += abc[(v >> 6) & 63];
            out += abc[v & 63];
        }
        if (in.size() - i == 1) {
            unsigned v = (unsigned char) in[i] << 16;
            out += abc[(v >> 18) & 63];
            out += abc[(v >> 12) & 63];
            out += "==";
        } else if (in.size() - i == 2) {
            unsigned v = ((unsigned char) in[i] << 16) | ((unsigned char) in[i + 1] << 8);
            out += abc[(v >> 18) & 63];
            out += abc[(v >> 12) & 63];
            out += abc[(v >> 6) & 63];
            out += '=';
        }
        return out;
    }

    // one get_short_epg entry as Xtream panels send it (times as strings, texts base64)
    std::string xtreamEntry(int64_t start, int64_t stop, const std::string &title, const std::string &description,
                            bool numericTimes = false) {
        std::string s = std::to_string(start);
        std::string e = std::to_string(stop);
        return "{\"id\":\"9\",\"epg_id\":\"3\",\"title\":\"" + b64(title) + "\",\"lang\":\"tr\","
               "\"start\":\"2026-11-17 20:00:00\",\"end\":\"2026-11-17 21:00:00\",\"description\":\"" + b64(description)
               + "\",\"channel_id\":\"trt1.tr\",\"start_timestamp\":" + (numericTimes ? s : "\"" + s + "\"")
               + ",\"stop_timestamp\":" + (numericTimes ? e : "\"" + e + "\"") + "}";
    }

    Programme prog(int64_t start, int64_t stop, const std::string &title) {
        Programme p;
        p.start = start;
        p.stop = stop;
        p.title = title;
        return p;
    }

    iptv::LiveChannel channel(const std::string &id, const std::string &epgId) {
        iptv::LiveChannel c;
        c.id = id;
        c.epgId = epgId;
        return c;
    }

    const int64_t T = 1763409600;   // 2025-11-17 20:00:00 UTC
}

TEST(epg_base64_texts) {
    CHECK_EQ(epg::decodeText(b64("Ak\xC5\x9F" "am Haberleri")), std::string("Ak\xC5\x9F" "am Haberleri"));
    CHECK_EQ(epg::decodeText(b64("\xD9\x86\xD8\xB4\xD8\xB1\xD8\xA9")), std::string("\xD9\x86\xD8\xB4\xD8\xB1\xD8\xA9"));
    CHECK_EQ(epg::decodeText("  " + b64("Film") + " \n"), std::string("Film"));   // whitespace around
    // plain text panels: never mangled into bytes
    CHECK_EQ(epg::decodeText("Evening News"), std::string("Evening News"));   // a space is not base64
    CHECK_EQ(epg::decodeText("News"), std::string("News"));                   // decodes to invalid UTF-8
    CHECK_EQ(epg::decodeText("Film"), std::string("Film"));                   // decodes to a control character
    CHECK_EQ(epg::decodeText("Spor"), std::string("Spor"));
    CHECK_EQ(epg::decodeText("Haber"), std::string("Haber"));                 // not a multiple of 4
    CHECK_EQ(epg::decodeText("/wA="), std::string("/wA="));                   // binary
    CHECK_EQ(epg::decodeText("QQ"), std::string("QQ"));                       // unpadded: left as is
    CHECK_EQ(epg::decodeText("QQ=A"), std::string("QQ=A"));                   // '=' inside
    CHECK_EQ(epg::decodeText(""), std::string(""));
}

TEST(epg_xtream_short_epg) {
    std::string body = "{\"epg_listings\":[" + xtreamEntry(T + 3600, T + 5400, "Spor Ekran\xC4\xB1", "Ma\xC3\xA7 \xC3\xB6zetleri") + ","
                       + xtreamEntry(T, T + 3600, "Ak\xC5\x9F" "am Haberleri", "G\xC3\xBCn\xC3\xBCn geli\xC5\x9Fmeleri.\nCanl\xC4\xB1.")
                       + "," + xtreamEntry(T + 5400, T + 7200, "Film", "", true) + "]}";
    std::vector<Programme> list;
    std::string err;
    CHECK(epg::parseXtream(body, list, err));
    CHECK_EQ(list.size(), (size_t) 3);
    CHECK_EQ(list[0].title, std::string("Ak\xC5\x9F" "am Haberleri"));   // sorted by start
    CHECK_EQ(list[0].start, T);
    CHECK_EQ(list[0].stop, T + 3600);
    CHECK_EQ(list[0].description, std::string("G\xC3\xBCn\xC3\xBCn geli\xC5\x9Fmeleri. Canl\xC4\xB1."));   // one line
    CHECK_EQ(list[1].title, std::string("Spor Ekran\xC4\xB1"));
    CHECK_EQ(list[2].title, std::string("Film"));   // numeric timestamps too
    CHECK(list[2].description.empty());

    // nothing for the stream: a valid, empty answer
    for (const char *empty: {"{\"epg_listings\":[]}", "{\"epg_listings\":null}", "{\"epg_listings\":false}", "[]"}) {
        CHECK(epg::parseXtream(empty, list, err) && list.empty());
    }
    // not a guide answer at all
    for (const char *bad: {"", "<html><body>404</body></html>", "{\"user_info\":{\"auth\":0}}", "{\"epg_listings\":"}) {
        CHECK(!epg::parseXtream(bad, list, err) && list.empty());
    }
}

TEST(epg_entries_are_never_repaired_or_invented) {
    std::string body = "{\"epg_listings\":["
                       // only the panel's local "start" / "end" strings: no usable time -> dropped, never guessed
                       "{\"title\":\"" + b64("No Times") + "\",\"start\":\"2026-11-17 20:00:00\",\"end\":\"2026-11-17 21:00:00\"},"
                       + xtreamEntry(T, T, "Zero Length", "") + ","
                       + xtreamEntry(T + 100, T, "Ends Before It Starts", "") + ","
                       + xtreamEntry(T, T + 25 * 3600, "Longer Than A Day", "") + ","
                       + xtreamEntry(0, 3600, "Epoch", "") + ","
                       + xtreamEntry(T, T + 1800, "", "a description without a title") + ","
                       + xtreamEntry(T, T + 1800, "N/A", "") + ","
                       + xtreamEntry(T, T + 1800, " - ", "") + ","
                       "\"not an object\",42,null]}";
    std::vector<Programme> list;
    std::string err;
    CHECK(epg::parseXtream(body, list, err));
    CHECK(list.empty());   // "no program information"
}

TEST(epg_duplicates_overlaps_caps_and_text_cleaning) {
    std::string entries;
    for (int i = 0; i < 12; i++) {
        entries += (i ? "," : "") + xtreamEntry(T + i * 1800, T + (i + 1) * 1800, "Show " + std::to_string(i), "");
    }
    entries += "," + xtreamEntry(T, T + 1800, "Show 0 again", "");               // the same slot twice
    entries += "," + xtreamEntry(T + 1700, T + 3300, "Overlapping", "");          // starts inside Show 0
    std::vector<Programme> list;
    std::string err;
    CHECK(epg::parseXtream("{\"epg_listings\":[" + entries + "]}", list, err));
    CHECK_EQ(list.size(), epg::MAX_PROGRAMMES);
    CHECK_EQ(list[0].title, std::string("Show 0"));
    CHECK_EQ(list[0].stop, T + 1700);    // runs until the next one the provider lists starts
    CHECK_EQ(list[1].title, std::string("Overlapping"));
    for (size_t i = 1; i < list.size(); i++) {
        CHECK(list[i].start >= list[i - 1].stop);   // sorted, never overlapping
    }

    // tabs, line breaks and runs of spaces: one clean line; long texts cut at a character boundary
    std::string longText;
    for (int i = 0; i < 400; i++) {
        longText += "\xC3\xBC";   // u with diaeresis, 2 bytes: the cut must not split it
    }
    CHECK(epg::parseXtream("{\"epg_listings\":[" + xtreamEntry(T, T + 60, "A\t\tB  C\r\nD", longText) + "]}",
                           list, err));
    CHECK_EQ(list.size(), (size_t) 1);
    CHECK_EQ(list[0].title, std::string("A B C D"));
    CHECK(list[0].description.size() <= epg::MAX_DESCRIPTION + 3);
    CHECK(list[0].description.compare(list[0].description.size() - 3, 3, "\xE2\x80\xA6") == 0);
    std::string body = list[0].description.substr(0, list[0].description.size() - 3);
    CHECK(body.size() % 2 == 0);   // whole characters only
    // a plain-text panel sending control characters: stripped (base64 that decodes to them is not decoded at all:
    // that is how plain words such as "Film" are told apart from base64)
    CHECK(epg::parseXtream("{\"epg_listings\":[{\"title\":\"Gece\\u0001\\u0002 Ku\\u015fu\",\"start_timestamp\":\""
                           + std::to_string(T) + "\",\"stop_timestamp\":\"" + std::to_string(T + 60) + "\"}]}", list, err));
    CHECK_EQ(list.size(), (size_t) 1);
    CHECK_EQ(list[0].title, std::string("Gece Ku\xC5\x9Fu"));
}

TEST(epg_stalker_short_epg) {
    std::string items = "[{\"id\":\"1\",\"ch_id\":\"77\",\"time\":\"2026-11-17 21:00:00\",\"time_to\":\"2026-11-17 22:00:00\","
                        "\"duration\":3600,\"name\":\"Gece Haberleri\",\"descr\":\"Son dakika\",\"start_timestamp\":"
                        + std::to_string(T + 3600) + ",\"stop_timestamp\":" + std::to_string(T + 7200) + "},"
                        "{\"id\":\"2\",\"ch_id\":\"77\",\"name\":\"Ak\xC5\x9F" "am\",\"descr\":\"\",\"start_timestamp\":\""
                        + std::to_string(T) + "\",\"stop_timestamp\":\"" + std::to_string(T + 3600) + "\"},"
                        "{\"id\":\"3\",\"ch_id\":\"77\",\"name\":\"No times\",\"time\":\"2026-11-17 23:00:00\"}]";
    std::vector<Programme> list;
    std::string err;
    CHECK(epg::parseStalker("{\"js\":" + items + "}", list, err));
    CHECK_EQ(list.size(), (size_t) 2);
    CHECK_EQ(list[0].title, std::string("Ak\xC5\x9F" "am"));
    CHECK_EQ(list[1].title, std::string("Gece Haberleri"));
    CHECK_EQ(list[1].description, std::string("Son dakika"));
    CHECK(epg::parseStalker("{\"js\":{\"data\":" + items + "}}", list, err) && list.size() == 2);
    for (const char *empty: {"{\"js\":[]}", "{\"js\":null}", "{\"js\":false}", "{\"js\":{}}"}) {
        CHECK(epg::parseStalker(empty, list, err) && list.empty());
    }
    for (const char *bad: {"Authorization failed.", "<html></html>", "{\"text\":\"no js\"}", ""}) {
        CHECK(!epg::parseStalker(bad, list, err) && list.empty());
    }
}

TEST(epg_now_next_progress_and_times) {
    std::vector<Programme> list = {prog(T, T + 600, "A"), prog(T + 600, T + 1200, "B"), prog(T + 1800, T + 2400, "C")};
    epg::NowNext nn = epg::nowNext(list, T + 300);
    CHECK(nn.now == 0 && nn.next == 1);
    nn = epg::nowNext(list, T + 600);          // starts are inclusive, ends exclusive
    CHECK(nn.now == 1 && nn.next == 2);
    nn = epg::nowNext(list, T + 1500);         // a gap: nothing on air, C next
    CHECK(nn.now == -1 && nn.next == 2);
    nn = epg::nowNext(list, T - 60);
    CHECK(nn.now == -1 && nn.next == 0);
    nn = epg::nowNext(list, T + 2400);         // everything ended
    CHECK(nn.now == -1 && nn.next == -1);
    CHECK(epg::nowNext({}, T).now == -1);

    CHECK_EQ(epg::progress(list[0], T - 10), 0.0);
    CHECK_EQ(epg::progress(list[0], T + 300), 0.5);
    CHECK_EQ(epg::progress(list[0], T + 900), 1.0);
    CHECK_EQ(epg::minutesLeft(list[0], T + 539), 2);   // 61 s: 2 minutes
    CHECK_EQ(epg::minutesLeft(list[0], T + 540), 1);
    CHECK_EQ(epg::minutesLeft(list[0], T + 600), 0);

    // the panel's times are local "HH:MM" (host: UTC); 2025-11-17 20:00:00 UTC
    CHECK_EQ(clockx::localTimeOf(T), std::string("20:00"));
    CHECK_EQ(clockx::localTimeOf(T + 45 * 60 + 59), std::string("20:45"));
}

TEST(epg_cache_asks_each_channel_rarely) {
    Cache c;
    const int64_t now = T + 300;
    CHECK(c.due("1", now));
    CHECK(c.get("1").state == Cache::State::Missing);
    c.loading("1", now);
    CHECK(!c.due("1", now + 10));                       // one request at a time
    CHECK(c.get("1").state == Cache::State::Loading);
    CHECK(c.due("1", now + Cache::LOADING_FOR));        // an answer that never came: asked again

    // a guide: asked again when the programme on air ends (here in 5 minutes)
    c.store("1", {prog(T, T + 600, "A"), prog(T + 600, T + 1200, "B")}, now);
    CHECK(c.get("1").state == Cache::State::Ready && c.get("1").programmes.size() == 2);
    CHECK(!c.due("1", T + 599));
    CHECK(c.due("1", T + 600));
    // ... but never sooner than a minute, never later than READY_FOR
    c.store("2", {prog(T, T + 330, "ends in 30 s")}, now);
    CHECK(!c.due("2", now + 59) && c.due("2", now + 60));
    c.store("3", {prog(T, T + 6 * 3600, "a long one")}, now);
    CHECK(!c.due("3", now + Cache::READY_FOR - 1) && c.due("3", now + Cache::READY_FOR));
    // nothing on air yet: asked again when the first one starts
    c.store("4", {prog(now + 900, now + 1800, "later")}, now);
    CHECK(c.get("4").state == Cache::State::Ready && !c.due("4", now + 899) && c.due("4", now + 900));

    // no program information (nothing, or only programmes that ended): asked again after EMPTY_FOR
    c.store("5", {}, now);
    CHECK(c.get("5").state == Cache::State::Empty);
    CHECK(!c.due("5", now + Cache::EMPTY_FOR - 1) && c.due("5", now + Cache::EMPTY_FOR));
    c.store("6", {prog(T - 7200, T - 3600, "yesterday's")}, now);
    CHECK(c.get("6").state == Cache::State::Empty && c.get("6").programmes.empty());
    // ended programmes are dropped from a stored answer
    c.store("7", {prog(T - 600, T, "ended"), prog(T, T + 600, "on air")}, now);
    CHECK(c.get("7").programmes.size() == 1 && c.get("7").programmes[0].title == "on air");

    // a failed request: tried again after FAILED_FOR; an older answer stays shown meanwhile
    c.loading("8", now);
    c.failed("8", now);
    CHECK(c.get("8").state == Cache::State::Failed);
    CHECK(!c.due("8", now + Cache::FAILED_FOR - 1) && c.due("8", now + Cache::FAILED_FOR));
    c.loading("1", T + 600);                            // a refresh of a shown guide keeps it on screen
    CHECK(c.get("1").state == Cache::State::Ready);
    c.failed("1", T + 600);
    CHECK(c.get("1").state == Cache::State::Ready && c.get("1").programmes.size() == 2);
    CHECK(c.due("1", T + 600 + Cache::FAILED_FOR));
}

TEST(epg_cache_is_bounded) {
    Cache c;
    for (int i = 0; i < 60; i++) {
        c.store("ch" + std::to_string(i), {}, T);
        c.get("ch0");   // in use all along: kept
    }
    CHECK_EQ(c.size(), Cache::MAX_CHANNELS);
    CHECK(c.get("ch0").state == Cache::State::Empty);
    CHECK(c.get("ch1").state == Cache::State::Missing);   // the least recently used went first
    CHECK(c.get("ch59").state == Cache::State::Empty);
}

TEST(epg_which_sources_are_asked) {
    using iptv::SourceType;
    CHECK(epg::sourceFor(SourceType::Xtream, channel("101", "trt1.tr")) == epg::Source::Xtream);
    CHECK(epg::sourceFor(SourceType::Xtream, channel("101", "")) == epg::Source::None);      // no EPG mapping
    CHECK(epg::sourceFor(SourceType::Stalker, channel("77", "")) == epg::Source::Stalker);   // by channel id
    CHECK(epg::sourceFor(SourceType::Stalker, channel("77", "trt1.tr")) == epg::Source::Stalker);
    CHECK(epg::sourceFor(SourceType::M3u, channel("m3u-1", "trt1.tr")) == epg::Source::None);  // XMLTV: never
    CHECK(epg::sourceFor(SourceType::Xtream, channel("", "trt1.tr")) == epg::Source::None);
}
