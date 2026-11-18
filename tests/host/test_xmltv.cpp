// XMLTV guides of playlist sources (iptv/xmltv.h) and the guide addresses of a playlist header (iptv/m3u.h): time
// stamps, the streaming reader (chunked input, entities, CDATA, encodings, languages, not-an-XMLTV input, bounded
// tokens), what the builder keeps (matched channels, the next hours, memory limits, stops from the next programme,
// duplicates / overlaps) and the saved guide file (round trip, matching, damaged files). Nothing may be made up.

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/iptv/epg.h"
#include "../../src/iptv/m3u.h"
#include "../../src/iptv/xmltv.h"
#include "../../src/platform/fs.h"

namespace {
    const int64_t T = 1763409600;   // 2025-11-17 20:00:00 UTC

    // "YYYYMMDDhhmmss +0000" of a unix time
    std::string stamp(int64_t t, const char *zone = " +0000") {
        int64_t days = t / 86400;
        int64_t secs = t % 86400;
        // civil from days (1970-01-01 = 0)
        days += 719468;
        int64_t era = days / 146097;
        int64_t doe = days - era * 146097;
        int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        int64_t y = yoe + era * 400;
        int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        int64_t mp = (5 * doy + 2) / 153;
        int64_t d = doy - (153 * mp + 2) / 5 + 1;
        int64_t m = mp + (mp < 10 ? 3 : -9);
        y += m <= 2;
        char buf[64];
        snprintf(buf, sizeof(buf), "%04lld%02lld%02lld%02lld%02lld%02lld%s", (long long) y, (long long) m,
                 (long long) d, (long long) (secs / 3600), (long long) (secs / 60 % 60), (long long) (secs % 60), zone);
        return buf;
    }

    std::string programme(const std::string &channel, int64_t start, int64_t stop, const std::string &title,
                          const std::string &desc = "") {
        std::string s = "<programme start=\"" + stamp(start) + "\"";
        if (stop > 0) {
            s += " stop=\"" + stamp(stop) + "\"";
        }
        s += " channel=\"" + channel + "\"><title lang=\"en\">" + title + "</title>";
        if (!desc.empty()) {
            s += "<desc>" + desc + "</desc>";
        }
        return s + "</programme>\n";
    }

    struct Collect : xmltv::Handler {
        std::vector<xmltv::Channel> channels;
        std::vector<xmltv::Programme> programmes;

        void channel(xmltv::Channel &c) override { channels.push_back(c); }

        void programme(xmltv::Programme &p) override { programmes.push_back(p); }
    };

    // feeds `xml` in pieces of `step` bytes
    bool read(const std::string &xml, Collect &out, size_t step, const std::string &lang = "") {
        xmltv::Reader r(out, lang);
        for (size_t i = 0; i < xml.size(); i += step) {
            if (!r.feed(xml.data() + i, std::min(step, xml.size() - i))) {
                return false;
            }
        }
        return r.finish();
    }

    std::string tmpDir() {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", "xmltv");
        fs::ensureDir(d);
        return d;
    }

    iptv::LiveChannel live(const std::string &tvgId, const std::string &name) {
        iptv::LiveChannel c;
        c.id = "m" + name;
        c.epgId = tvgId;
        c.name = name;
        c.url = "http://stream.example.com/1.ts";
        return c;
    }

    // feed -> builder -> file -> index
    bool build(const std::string &xml, xmltv::Wanted wanted, xmltv::Index &index, xmltv::BuildStats *stats = nullptr,
               xmltv::BuildLimits limits = xmltv::BuildLimits(), const std::string &file = "guide.bin") {
        xmltv::Builder b(std::move(wanted), T, limits);
        xmltv::Reader r(b, "tr");
        if (!r.feed(xml.data(), xml.size()) || !r.finish()) {
            return false;
        }
        std::string err;
        std::string path = fs::join(tmpDir(), file);
        if (!b.write(path, 42, err)) {
            return false;
        }
        if (stats) {
            *stats = b.stats();
        }
        return index.open(path, err);
    }
}

TEST(xmltv_times) {
    int64_t t = 0;
    CHECK(xmltv::parseTime("20251117200000 +0000", t));
    CHECK_EQ(t, T);
    CHECK(xmltv::parseTime("20251117230000 +0300", t));   // 23:00 in UTC+3 is 20:00 UTC
    CHECK_EQ(t, T);
    CHECK(xmltv::parseTime("20251117150000 -0500", t));
    CHECK_EQ(t, T);
    CHECK(xmltv::parseTime("20251117210000+01:00", t));
    CHECK_EQ(t, T);
    CHECK(xmltv::parseTime("20251117200000", t));         // no zone: UTC (the XMLTV rule)
    CHECK_EQ(t, T);
    CHECK(xmltv::parseTime("202511172000 Z", t));         // no seconds
    CHECK_EQ(t, T);
    CHECK(xmltv::parseTime("20251117200000 UTC", t));
    CHECK_EQ(t, T);
    CHECK(xmltv::parseTime("20240229120000 +0000", t));   // a leap day
    // never guessed: unknown zones, impossible dates, partial stamps
    CHECK(!xmltv::parseTime("20251117200000 EST", t));
    CHECK(!xmltv::parseTime("20250229120000 +0000", t));
    CHECK(!xmltv::parseTime("20251317200000 +0000", t));
    CHECK(!xmltv::parseTime("20251117250000 +0000", t));
    CHECK(!xmltv::parseTime("2025111720", t));
    CHECK(!xmltv::parseTime("", t));
    CHECK(!xmltv::parseTime("20251117200000 +2500", t));
    CHECK(!xmltv::parseTime("1763409600", t));             // a unix time is not an XMLTV stamp
}

TEST(xmltv_reader_basics) {
    std::string xml = "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n"
                      "<!-- generated <by> a tool -->\n"
                      "<tv generator-info-name=\"x\">\n"
                      "<channel id=\"trt1.tr\"><display-name lang=\"tr\">TRT 1</display-name>"
                      "<display-name>TRT1 HD</display-name><icon src=\"http://img.example.com/a.png\"/></channel>\n"
                      "<channel id='bbc1.uk'/>\n"
                      "<programme start=\"20251117200000 +0000\" stop=\"20251117210000 +0000\" channel=\"trt1.tr\">"
                      "<title lang=\"en\">News &amp; Weather</title><title lang=\"tr\">Haberler &#x26; Hava</title>"
                      "<sub-title>ignored</sub-title><desc lang=\"tr\"><![CDATA[Gündem <canlı> & özet]]></desc>"
                      "<credits><actor>Someone</actor></credits><category>News</category></programme>\n"
                      "<programme start=\"20251117210000 +0000\" channel=\"trt1.tr\"><title>Film &#233;t&#xE9;</title>"
                      "</programme>\n"
                      "<programme start=\"bad\" stop=\"20251117210000 +0000\" channel=\"trt1.tr\"><title>X</title>"
                      "</programme>\n"
                      "<programme start=\"20251117220000 +0000\" channel=\"trt1.tr\"><desc>no title</desc></programme>\n"
                      "<programme start=\"20251117230000 +0000\" channel=\"trt1.tr\"/>\n"
                      "</tv>\n";
    for (size_t step: {(size_t) 1, (size_t) 7, (size_t) 64, xml.size()}) {
        Collect c;
        CHECK(read(xml, c, step, "tr"));
        CHECK_EQ(c.channels.size(), (size_t) 2);
        if (c.channels.size() == 2) {
            CHECK_EQ(c.channels[0].id, std::string("trt1.tr"));
            CHECK_EQ(c.channels[0].names.size(), (size_t) 2);
            CHECK_EQ(c.channels[0].names[0], std::string("TRT 1"));
            CHECK_EQ(c.channels[1].id, std::string("bbc1.uk"));
            CHECK(c.channels[1].names.empty());
        }
        CHECK_EQ(c.programmes.size(), (size_t) 2);   // bad start, no title, self-closed: dropped
        if (c.programmes.size() == 2) {
            CHECK_EQ(c.programmes[0].start, T);
            CHECK_EQ(c.programmes[0].stop, T + 3600);
            CHECK_EQ(c.programmes[0].title, std::string("Haberler & Hava"));   // the preferred language wins
            CHECK_EQ(c.programmes[0].description, std::string("Gündem <canlı> & özet"));
            CHECK_EQ(c.programmes[1].stop, (int64_t) 0);                    // no stop given
            CHECK_EQ(c.programmes[1].title, std::string("Film \xC3\xA9t\xC3\xA9"));
        }
    }
    // without a preferred language: the first title
    Collect c;
    CHECK(read(xml, c, 13));
    CHECK(c.programmes.size() == 2 && c.programmes[0].title == "News & Weather");
}

TEST(xmltv_reader_rejects_and_bounds) {
    Collect c;
    CHECK(!read("<!DOCTYPE html><html><body>Error 403</body></html>", c, 5));
    CHECK(!read("{\"error\":\"not found\"}", c, 5));
    CHECK(!read("", c, 5));
    CHECK(!read("<?xml version=\"1.0\"?><rss><channel/></rss>", c, 5));
    // ISO-8859-1 declared: converted to UTF-8
    std::string latin = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><tv><programme start=\"20251117200000\" "
                        "channel=\"a\"><title>Caf\xE9 \x93X\x94</title></programme></tv>";
    Collect l;
    CHECK(read(latin, l, 3));
    CHECK(l.programmes.size() == 1 && l.programmes[0].title == "Caf\xC3\xA9 \xE2\x80\x9CX\xE2\x80\x9D");
    // an endless text node / tag never grows the buffer without bound; the rest still reads
    std::string huge(xmltv::Reader::MAX_TOKEN + 20000, 'x');   // longer than the bound plus one piece of input
    std::string xml = "<tv><programme start=\"20251117200000\" channel=\"a\"><title>" + huge + "</title></programme>"
                      "<programme start=\"20251117210000\" channel=\"a\" junk=\"" + huge + "\"><title>B</title>"
                      "</programme><programme start=\"20251117220000\" channel=\"a\"><title>C</title></programme></tv>";
    Collect h;
    xmltv::Reader r(h);
    bool ok = true;
    for (size_t i = 0; i < xml.size() && ok; i += 4096) {
        ok = r.feed(xml.data() + i, std::min((size_t) 4096, xml.size() - i));
    }
    CHECK(ok && r.finish());
    CHECK_EQ(r.stats().oversized, 2);
    CHECK(!h.programmes.empty() && h.programmes.back().title == "C");
    // a title text is kept up to MAX_FIELD only
    std::string longTitle(xmltv::Reader::MAX_FIELD * 3, 'y');
    Collect t;
    CHECK(read("<tv><programme start=\"20251117200000\" channel=\"a\"><title>" + longTitle + "</title></programme></tv>",
               t, 1000));
    CHECK(t.programmes.size() == 1 && t.programmes[0].title.size() == xmltv::Reader::MAX_FIELD);
}

// what is not XML is refused with the first piece that shows it, not after the whole download (an answer of
// "[" and 80 MiB of spaces kept a guide download busy for over a minute: failure_it); white space and a byte order
// mark split between pieces still wait for the document
TEST(xmltv_reader_rejects_non_xml_at_once) {
    for (const std::string &start: {std::string("["), std::string("{\"error\": 1"), std::string("\x00\x01\x02", 3),
                                    std::string("Error 500")}) {
        Collect c;
        xmltv::Reader r(c);
        CHECK(!r.feed(start.data(), start.size()));
        std::string spaces(4096, ' ');
        CHECK(!r.feed(spaces.data(), spaces.size()));
        CHECK(r.stats().bytes < 8192);
    }
    const std::string doc = "\xEF\xBB\xBF  \n\t<?xml version=\"1.0\"?><tv><programme start=\"20251117200000\" "
                            "channel=\"a\"><title>A</title></programme></tv>";
    for (size_t piece: {(size_t) 1, (size_t) 2, (size_t) 5, doc.size()}) {
        Collect c;
        xmltv::Reader r(c);
        bool ok = true;
        for (size_t i = 0; i < doc.size() && ok; i += piece) {
            ok = r.feed(doc.data() + i, std::min(piece, doc.size() - i));
        }
        CHECK(ok && r.finish());
        CHECK(c.programmes.size() == 1 && c.programmes[0].title == "A");
    }
}

TEST(xmltv_builder_keeps_only_what_is_needed) {
    std::string xml = "<tv>"
                      "<channel id=\"trt1.tr\"><display-name>TRT 1</display-name></channel>"
                      "<channel id=\"Show.TR\"><display-name>Show TV</display-name></channel>"
                      "<channel id=\"other.de\"><display-name>Das Erste</display-name></channel>"
                      "<channel id=\"named.x\"><display-name>Kanal Ç</display-name></channel>"
                      "<channel id=\"twin1\"><display-name>Twin</display-name></channel>"
                      "<channel id=\"twin2\"><display-name>Twin</display-name></channel>";
    // trt1.tr: ended / on air / later; a missing stop ends at the next start; same slot twice; an overlap
    xml += programme("trt1.tr", T - 7200, T - 3600, "Ended");
    xml += programme("trt1.tr", T - 1800, T + 1800, "On air", "Line one\nline   two");
    xml += programme("trt1.tr", T + 1800, 0, "No stop");
    xml += programme("trt1.tr", T + 1830, T + 3000, "Same slot");
    xml += programme("trt1.tr", T + 3600, T + 9000, "Overlaps");
    xml += programme("trt1.tr", T + 7200, T + 10800, "Ends the overlap");
    xml += programme("trt1.tr", T + 40 * 3600, T + 41 * 3600, "Too far ahead");
    xml += programme("trt1.tr", T + 20000, 0, "Last, no stop");          // nothing after it: dropped
    xml += programme("Show.TR", T, T + 3600, "Matched ignoring case");
    xml += programme("other.de", T, T + 3600, "Not in the playlist");
    xml += programme("named.x", T, T + 3600, "Matched by name");
    xml += programme("twin1", T, T + 3600, "Twin one");
    xml += programme("twin2", T, T + 3600, "Twin two");
    xml += programme("undescribed.id", T, T + 3600, "No channel element, id matched");
    xml += programme("trt1.tr", T - 30 * 3600, 0, "Started a day ago, no stop");
    xml += "</tv>";

    xmltv::Wanted w;
    w.add("trt1.tr", "TRT 1 HD");
    w.add("show.tr", "Show");
    w.add("", "kanal c");          // no tvg-id: matched by its (folded) name
    w.add("none", "Twin");         // an unusable tvg-id; the name is ambiguous in the guide
    w.add("undescribed.id", "U");
    xmltv::Index idx;
    xmltv::BuildStats bs;
    CHECK(build(xml, w, idx, &bs));
    CHECK_EQ(bs.matchedChannels, 6);   // trt1, Show.TR, named.x, twin1, twin2, undescribed.id
    CHECK(bs.unmatchedProgrammes == 1);
    CHECK_EQ(idx.savedAt, T);
    CHECK_EQ(idx.sourceKey, (uint64_t) 42);

    std::vector<epg::Programme> p;
    std::string err;
    int trt = idx.find(live("trt1.tr", "TRT 1 HD"));
    CHECK(trt >= 0);
    CHECK(idx.read(trt, T, p, err));
    CHECK_EQ(p.size(), (size_t) 4);
    if (p.size() == 4) {
        CHECK(p[0].title == "On air" && p[0].start == T - 1800 && p[0].stop == T + 1800);
        CHECK_EQ(p[0].description, std::string("Line one line two"));
        CHECK(p[1].title == "No stop" && p[1].stop == T + 3600);   // ends where "Overlaps" starts (Same slot dropped)
        CHECK(p[2].title == "Overlaps" && p[2].stop == T + 7200);  // cut at the next start
        CHECK(p[3].title == "Ends the overlap");
    }
    CHECK_EQ(bs.coverageEnd, T + 10800);
    CHECK_EQ(idx.coverageEnd, T + 10800);
    // later: what ended is not returned
    CHECK(idx.read(trt, T + 7300, p, err) && p.size() == 1 && p[0].title == "Ends the overlap");

    int show = idx.find(live("SHOW.tr", "x"));
    CHECK(show >= 0 && idx.read(show, T, p, err) && p.size() == 1 && p[0].title == "Matched ignoring case");
    int named = idx.find(live("", "KANAL  ç"));
    CHECK(named >= 0 && idx.read(named, T, p, err) && p.size() == 1 && p[0].title == "Matched by name");
    CHECK_EQ(idx.find(live("none", "Twin")), -1);          // two guide channels with the name: never guessed
    CHECK_EQ(idx.find(live("other.de", "Das Erste")), -1); // not kept: the playlist has no such channel
    CHECK_EQ(idx.find(live("missing.id", "Nothing")), -1);
    CHECK(idx.find(live("undescribed.id", "U")) >= 0);
    // a tvg-id the guide lacks falls back to the name
    CHECK_EQ(idx.find(live("trt1-other.tr", "TRT 1")), trt);
    CHECK(!idx.read(999, T, p, err));
}

TEST(xmltv_builder_limits) {
    xmltv::Wanted w;
    for (int i = 0; i < 4; i++) {
        w.add("c" + std::to_string(i), "");
    }
    std::string xml = "<tv>";
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 30; k++) {
            xml += programme("c" + std::to_string(i), T + k * 600, T + (k + 1) * 600, "P" + std::to_string(k),
                             std::string(300, 'd'));
        }
    }
    xml += "</tv>";
    xmltv::BuildLimits l;
    l.maxPerChannel = 10;
    l.maxProgrammes = 100;
    l.maxTextBytes = 20000;   // descriptions: ~49 programmes' worth (3/4), then titles only
    xmltv::Index idx;
    xmltv::BuildStats bs;
    CHECK(build(xml, w, idx, &bs, l, "limits.bin"));
    CHECK_EQ(bs.overLimit, 20);              // 120 offered, 100 held at most
    CHECK(bs.descriptionsDropped > 0);       // titles kept, descriptions dropped once the text budget is used
    CHECK_EQ(bs.kept, 40);                   // 10 per channel
    std::vector<epg::Programme> p;
    std::string err;
    int c0 = idx.find(live("c0", "x"));
    CHECK(c0 >= 0 && idx.read(c0, T, p, err));
    CHECK_EQ(p.size(), epg::MAX_PROGRAMMES);  // a read returns at most what the cache keeps
    CHECK(!p.empty() && p[0].description.size() == 300);
    int c3 = idx.find(live("c3", "x"));
    CHECK(c3 >= 0 && idx.read(c3, T, p, err) && !p.empty() && p[0].description.empty());
}

TEST(xmltv_saved_guide_damage) {
    xmltv::Wanted w;
    w.add("a", "A");
    std::string xml = "<tv>" + programme("a", T, T + 3600, "A1") + programme("a", T + 3600, T + 7200, "A2") + "</tv>";
    xmltv::Index idx;
    CHECK(build(xml, w, idx, nullptr, xmltv::BuildLimits(), "damage.bin"));
    std::string path = fs::join(tmpDir(), "damage.bin");
    std::string data;
    CHECK(fs::readFile(path, data, 1 << 20));
    std::string err;
    // a flipped byte in the channel table, a cut file, another version, garbage, no file
    std::string flipped = data;
    flipped[flipped.size() - 3] ^= 0x55;
    std::string cut = data.substr(0, data.size() - 5);
    std::string version = data;
    version[8] = 99;
    for (const std::string &bad: {flipped, cut, version, std::string("V7XMLTV"), std::string(200, '\0')}) {
        CHECK(fs::writeFileReplace(path, bad));
        xmltv::Index broken;
        CHECK(!broken.open(path, err));
        CHECK_EQ(broken.size(), (size_t) 0);
        CHECK_EQ(broken.find(live("a", "A")), -1);
    }
    xmltv::Index none;
    CHECK(!none.open(fs::join(tmpDir(), "does-not-exist.bin"), err));
    // an Index whose file disappeared: reading fails, nothing is returned
    CHECK(fs::writeFileReplace(path, data));
    xmltv::Index ok;
    CHECK(ok.open(path, err));
    int a = ok.find(live("a", "A"));
    std::vector<epg::Programme> p;
    CHECK(a >= 0 && ok.read(a, T, p, err) && p.size() == 2);
    // a newer guide written over it: the old Index never reads the new file with its old offsets
    xmltv::Wanted w2;
    w2.add("zz", "Z");
    w2.add("a", "A");
    std::string xml2 = "<tv>" + programme("zz", T, T + 600, std::string(500, 'z')) + programme("a", T, T + 60, "New")
                       + "</tv>";
    xmltv::Index newer;
    CHECK(build(xml2, w2, newer, nullptr, xmltv::BuildLimits(), "damage.bin"));
    bool gone = false;
    CHECK(!ok.read(a, T, p, err, &gone) && gone && p.empty());
    int na = newer.find(live("a", "A"));
    CHECK(na >= 0 && newer.read(na, T, p, err, &gone) && !gone && p.size() == 1 && p[0].title == "New");
    fs::removeFile(path);
    CHECK(!newer.read(na, T, p, err, &gone) && gone && p.empty());
    CHECK(!ok.read(a, T, p, err) && p.empty());
    // the source key changes with the addresses
    CHECK(xmltv::sourceKey({"http://a.example.com/e.xml"}) != xmltv::sourceKey({"http://b.example.com/e.xml"}));
    CHECK(xmltv::sourceKey({}) != xmltv::sourceKey({"http://a.example.com/e.xml"}));
    CHECK_EQ(xmltv::nameKey("  TRT   1\tHD "), std::string("trt 1 hd"));
    CHECK_EQ(xmltv::nameKey("Şow TV"), std::string("sow tv"));
}

TEST(m3u_guide_addresses) {
    m3u::Result r = m3u::parse("#EXTM3U url-tvg=\"http://epg.example.com/a.xml.gz, http://epg.example.com/b.xml\" "
                               "x-tvg-url=http://epg.example.com/a.xml.gz tvg-url='https://epg.example.com/c.xml' "
                               "url-tvg=\"http://epg.example.com/d.xml\" refresh=3600\n"
                               "#EXTINF:-1 tvg-id=\"a\",A\nhttp://stream.example.com/a.ts\n");
    CHECK(r.ok());
    CHECK_EQ(r.guideUrls.size(), (size_t) m3u::MAX_GUIDE_URLS);   // a, b, c (d over the limit; a only once)
    if (r.guideUrls.size() == 3) {
        CHECK_EQ(r.guideUrls[0], std::string("http://epg.example.com/a.xml.gz"));
        CHECK_EQ(r.guideUrls[1], std::string("http://epg.example.com/b.xml"));
        CHECK_EQ(r.guideUrls[2], std::string("https://epg.example.com/c.xml"));
    }
    CHECK_EQ(r.stats.guideUrls, 3);
    // only http(s) addresses; none at all
    m3u::Result l = m3u::parse("#EXTM3U url-tvg=\"/data/epg.xml\" x-tvg-url=\"ftp://epg.example.com/e.xml\"\n"
                               "#EXTINF:-1,A\nhttp://stream.example.com/a.ts\n");
    CHECK(l.ok() && l.guideUrls.empty());
    m3u::Result n = m3u::parse("#EXTM3U\n#EXTINF:-1,A\nhttp://stream.example.com/a.ts\n");
    CHECK(n.ok() && n.guideUrls.empty());
    // the catalog carries them; sources: a playlist channel has a guide only when its playlist names one
    iptv::LiveCatalog cat;
    m3u::Info info;
    CHECK(m3u::build("p1", "#EXTM3U x-tvg-url=\"http://epg.example.com/a.xml\"\n#EXTINF:-1 tvg-id=\"a\",A\n"
                           "http://stream.example.com/a.ts\n", cat, info) == m3u::Error::None);
    CHECK_EQ(cat.guideUrls.size(), (size_t) 1);
    CHECK(!cat.channels().empty());
    if (!cat.channels().empty()) {
        const iptv::LiveChannel &c = cat.channels()[0];
        CHECK(epg::sourceFor(iptv::SourceType::M3u, c, true) == epg::Source::Xmltv);
        CHECK(epg::sourceFor(iptv::SourceType::M3u, c, false) == epg::Source::None);
        CHECK(epg::sourceFor(iptv::SourceType::M3u, c) == epg::Source::None);
    }
    CHECK(m3u::usableTvgId("trt1.tr") && !m3u::usableTvgId("N/A") && !m3u::usableTvgId(""));
    // the wanted set of a catalog
    xmltv::Wanted w = xmltv::Wanted::of(cat);
    CHECK(w.ids.count("a") == 1 && w.names.count("a") == 1);
}
