// Subtitles (GitHub issue #1, "arabic subtitles lag"): the subtitle font installed for mpv / libass, and what of
// mpv's log reaches log.txt (libass font-search floods, other bursts). The rendering itself is measured with the
// PS4's libass 0.14.0 configuration by tests/subtitles/run-subtitle-tests.sh.

#include <cstdlib>
#include <cstring>

#include "check.h"
#include "../../src/platform/fs.h"
#include "../../src/platform/log.h"
#include "../../src/platform/redact.h"
#include "../../src/player/mpv_log_filter.h"
#include "../../src/player/subtitle_font.h"

namespace {
    std::string tempDir(const char *name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", name);
        fs::ensureDir(d);
        for (const auto &e: fs::listDir(d)) {
            if (!e.dir) {
                fs::removeFile(fs::join(d, e.name));
            }
        }
        return d;
    }

    std::string readAll(const std::string &path) {
        std::string s;
        fs::readFile(path, s, 64u * 1024 * 1024);
        return s;
    }

    // the TrueType file's cmap has a glyph for every code point (format 4 / 12 subtables)
    bool fontHas(const std::string &font, uint32_t cp) {
        auto u16 = [&](size_t o) { return o + 2 <= font.size() ? (uint32_t) ((uint8_t) font[o] << 8 | (uint8_t) font[o + 1]) : 0u; };
        auto u32 = [&](size_t o) { return (u16(o) << 16) | u16(o + 2); };
        uint32_t tables = u16(4);
        size_t cmap = 0;
        for (uint32_t i = 0; i < tables; i++) {
            if (font.compare(12 + 16 * i, 4, "cmap") == 0) {
                cmap = u32(12 + 16 * i + 8);
            }
        }
        if (cmap == 0) {
            return false;
        }
        uint32_t n = u16(cmap + 2);
        for (uint32_t i = 0; i < n; i++) {
            size_t sub = cmap + u32(cmap + 4 + 8 * i + 4);
            uint32_t format = u16(sub);
            if (format == 4 && cp <= 0xFFFF) {
                uint32_t segs = u16(sub + 6) / 2;
                size_t ends = sub + 14, starts = ends + 2 * segs + 2, deltas = starts + 2 * segs, ranges = deltas + 2 * segs;
                for (uint32_t s = 0; s < segs; s++) {
                    uint32_t end = u16(ends + 2 * s), start = u16(starts + 2 * s);
                    if (cp < start || cp > end) {
                        continue;
                    }
                    uint32_t ro = u16(ranges + 2 * s);
                    uint32_t glyph;
                    if (ro == 0) {
                        glyph = (cp + u16(deltas + 2 * s)) & 0xFFFF;
                    } else {
                        uint32_t g = u16(ranges + 2 * s + ro + 2 * (cp - start));
                        glyph = g ? (g + u16(deltas + 2 * s)) & 0xFFFF : 0;
                    }
                    if (glyph != 0) {
                        return true;
                    }
                }
            } else if (format == 12) {
                uint32_t groups = u32(sub + 12);
                for (uint32_t g = 0; g < groups; g++) {
                    size_t o = sub + 16 + 12 * g;
                    if (cp >= u32(o) && cp <= u32(o + 4)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    std::string repoFile(const char *rel) {
        const char *src = std::getenv("PS4IPTV_SOURCE_DIR");   // <repo>/src
        return std::string(src ? src : "src") + "/../" + rel;
    }
}

TEST(subtitle_font_covers_arabic_persian_hebrew_and_bidi_marks) {
    std::string font = readAll(repoFile("third_party/fonts-subtitles/V7Subtitles.ttf"));
    std::string inter = readAll(repoFile("third_party/inter/Inter-SemiBold.ttf"));
    CHECK(font.size() > 100000);
    // every character of the issue's fixtures: Arabic letters, Arabic punctuation and digits, presentation forms
    // (libass 0.14 without HarfBuzz shapes with FriBidi into them), lam-alef ligatures, harakat, Persian letters,
    // Hebrew, and the invisible bidi marks Arabic SRT files carry (RLM before every line)
    for (uint32_t cp: {0x0627u, 0x0628u, 0x062Au, 0x0644u, 0x0645u, 0x0646u, 0x0647u, 0x0648u, 0x064Au, 0x0629u,
                       0x060Cu, 0x061Bu, 0x061Fu, 0x066Au, 0x0660u, 0x0669u, 0x064Bu, 0x064Eu, 0x064Fu, 0x0650u,
                       0x0651u, 0x0652u, 0x0670u, 0xFE8Du, 0xFE8Eu, 0xFEDFu, 0xFEE0u, 0xFEFBu, 0xFEFCu, 0x067Eu,
                       0x0686u, 0x0698u, 0x06AFu, 0x06A9u, 0x06CCu, 0x06F0u, 0x06F9u, 0xFB56u, 0xFB8Au, 0xFBFCu,
                       0x05D0u, 0x05D1u, 0x05E9u, 0x05EAu, 0xFB2Au, 0x200Eu, 0x200Fu, 0x202Au, 0x202Bu, 0x202Cu,
                       0x202Du, 0x202Eu, 0x2066u, 0x2067u, 0x2068u, 0x2069u, 0x061Cu, 0x200Cu, 0x200Du}) {
        if (!fontHas(font, cp)) {
            std::printf("  missing U+%04X\n", (unsigned) cp);
        }
        CHECK(fontHas(font, cp));
    }
    // and still every Latin / Turkish character Inter has (the merge keeps Inter's own glyphs)
    for (uint32_t cp: {0x41u, 0x7Au, 0x30u, 0x131u, 0x130u, 0x15Fu, 0x11Fu, 0xFCu, 0xF6u, 0xE7u, 0xE9u, 0x3A9u,
                       0x416u, 0x20ACu, 0x2026u}) {
        CHECK(fontHas(inter, cp));
        CHECK(fontHas(font, cp));
    }
    // the reason for the merge: Inter alone has none of them
    CHECK(!fontHas(inter, 0x0627));
    CHECK(!fontHas(inter, 0x05D0));
    CHECK(!fontHas(inter, 0x200F));
}

TEST(subtitle_font_is_installed_once_and_replaced_when_it_changes) {
    std::string fonts = tempDir("subfont_pkg");
    std::string mpv = tempDir("subfont_mpv");
    // an older package: only Inter -> Inter is used, exactly as before
    fs::writeFileReplace(fs::join(fonts, "Inter-SemiBold.ttf"), std::string(1000, 'I'));
    subfont::Result r = subfont::install(fonts, mpv, "0.3.0");
    CHECK(r.ok && r.installed);
    CHECK_EQ(r.source, std::string("Inter-SemiBold.ttf"));
    CHECK_EQ(readAll(fs::join(mpv, "subfont.ttf")), std::string(1000, 'I'));
    r = subfont::install(fonts, mpv, "0.3.0");
    CHECK(r.ok && !r.installed);   // nothing copied at every start
    // the subtitle font is preferred as soon as the package has it
    fs::writeFileReplace(fs::join(fonts, "V7Subtitles.ttf"), std::string(1200, 'V'));
    r = subfont::install(fonts, mpv, "0.4.0");
    CHECK(r.ok && r.installed);
    CHECK_EQ(r.source, std::string("V7Subtitles.ttf"));
    CHECK_EQ(readAll(fs::join(mpv, "subfont.ttf")), std::string(1200, 'V'));
    // a replaced font of the SAME size in a new version is copied too (the old check compared sizes only)
    fs::writeFileReplace(fs::join(fonts, "V7Subtitles.ttf"), std::string(1200, 'W'));
    r = subfont::install(fonts, mpv, "0.4.1");
    CHECK(r.ok && r.installed);
    CHECK_EQ(readAll(fs::join(mpv, "subfont.ttf")), std::string(1200, 'W'));
    // an interrupted copy (marker missing) is repeated
    fs::removeFile(fs::join(mpv, "subfont.id"));
    r = subfont::install(fonts, mpv, "0.4.1");
    CHECK(r.ok && r.installed);
    // no font in the package: reported, nothing written
    std::string empty = tempDir("subfont_none");
    std::string mpv2 = tempDir("subfont_mpv2");
    r = subfont::install(empty, mpv2, "0.4.0");
    CHECK(!r.ok && !r.installed && !r.error.empty());
    CHECK(!fs::exists(fs::join(mpv2, "subfont.ttf")));
}

TEST(libass_font_search_lines_are_recognised) {
    uint32_t cp = 1;
    CHECK(mpvlog::isFontSearchLine("Glyph 0x627 not found, selecting one more font for (sans-serif, 400, 0)", &cp));
    CHECK_EQ(cp, 0x627u);
    CHECK(mpvlog::isFontSearchLine("Glyph 0x644 not found in font for (sans-serif, 400, 0)", &cp));
    CHECK_EQ(cp, 0x644u);
    CHECK(mpvlog::isFontSearchLine("Glyph 0x5D0 not found, broken font? Trying all charmaps", &cp));
    CHECK(mpvlog::isFontSearchLine("Got a font face that already is available! Skipping.", &cp));
    CHECK_EQ(cp, 0u);
    CHECK(mpvlog::isFontSearchLine("fontselect: Using default font: (sans-serif, 400, 0) -> /data/x/subfont.ttf, 0, (none)"));
    CHECK(mpvlog::isFontSearchLine("fontselect: (Arial, 700, 0) -> /data/x/subfont.ttf, 0, (none)"));
    // out-of-range / garbage numbers never produce a code point above U+10FFFF
    CHECK(mpvlog::isFontSearchLine("Glyph 0xFFFFFFFFFFFF not found in font for (x, 0, 0)", &cp));
    CHECK_EQ(cp, 0u);
    // ordinary lines are not font searches
    CHECK(!mpvlog::isFontSearchLine("compile_attach_shader: precompiled shader not found"));
    CHECK(!mpvlog::isFontSearchLine("HTTP error 404 Not Found"));
    CHECK(!mpvlog::isFontSearchLine("Glyph cache full"));
    CHECK(!mpvlog::isFontSearchLine(""));
}

TEST(font_search_flood_is_summarised_not_written) {
    // the issue's numbers: an Arabic line with a Latin-only font = ~80 font-search lines per frame at 30 fps
    mpvlog::Filter f;
    int written = 0;
    double t = 100;
    for (int frame = 0; frame < 30 * 60; frame++, t += 1.0 / 30) {   // one minute
        for (int g = 0; g < 16; g++) {
            uint32_t cp = 0x627 + (uint32_t) g;
            char line[128];
            std::snprintf(line, sizeof(line), "Glyph 0x%X not found, selecting one more font for (sans-serif, 400, 0)", cp);
            written += f.admit(line, false, t);           // verbose: the log would drop it anyway
            written += f.admit("fontselect: Using default font: (sans-serif, 400, 0) -> /x/subfont.ttf, 0, (none)", true, t);
            std::snprintf(line, sizeof(line), "Glyph 0x%X not found in font for (sans-serif, 400, 0)", cp);
            written += f.admit(line, true, t);
        }
        // ordinary lines keep flowing
        if (frame % 30 == 0) {
            CHECK(f.admit("cache: 2.0 s", true, t));
        }
        f.takeSummary(t);
    }
    CHECK(written <= mpvlog::Filter::FONT_LINES_KEPT);   // 86,400 font lines -> at most 6 written
    CHECK_EQ((int) f.missingCodePoints().size(), 16);
    CHECK(f.missingCodePoints().count(0x627));
    std::string end = f.endSession();
    CHECK(end.find("no glyph for 16 character(s)") != std::string::npos);
    CHECK(end.find("U+0627") != std::string::npos);
    CHECK(end.find("U+0636") != std::string::npos);
    // the session ended: the next file starts with fresh counters and keeps its first lines again
    CHECK(f.missingCodePoints().empty());
    CHECK(f.admit("fontselect: (Arial, 400, 0) -> /x/subfont.ttf, 0, (none)", true, t));
    CHECK(f.endSession().empty());   // nothing was held back
}

TEST(font_search_summary_is_periodic_and_lists_a_bounded_number_of_code_points) {
    mpvlog::Filter f;
    double t = 0;
    int summaries = 0;
    for (int frame = 0; frame < 30 * 120; frame++, t += 1.0 / 30) {   // two minutes, 200 different characters
        char line[96];
        std::snprintf(line, sizeof(line), "Glyph 0x%X not found, selecting one more font for (x, 400, 0)",
                      0x4E00 + frame % 200);
        f.admit(line, false, t);
        std::string s = f.takeSummary(t);
        if (!s.empty()) {
            summaries++;
            CHECK(s.size() < 600);
        }
    }
    CHECK(summaries >= 3 && summaries <= 5);   // every 30 s, not every frame
    CHECK((int) f.missingCodePoints().size() <= mpvlog::Filter::MAX_CODE_POINTS);
}

TEST(mpv_log_bursts_are_rate_limited_and_reported) {
    mpvlog::Filter f;
    // a damaged live stream: FFmpeg prints a decode error per frame, 400 lines in one second
    int written = 0;
    for (int i = 0; i < 400; i++) {
        written += f.admit("[ffmpeg/video] h264: error while decoding MB 12 34", true, 10.0 + i * 0.0025);
    }
    CHECK_EQ(written, mpvlog::Filter::LINES_PER_SECOND);
    CHECK(f.takeSummary(10.5).empty());   // reported when the second is over, once
    std::string s = f.takeSummary(11.01);
    CHECK(s.find("340 mpv log line(s) not written") != std::string::npos);
    CHECK(f.takeSummary(11.02).empty());
    CHECK(f.admit("next second", true, 11.03));
    // verbose lines the log drops anyway never use the budget
    mpvlog::Filter g;
    for (int i = 0; i < 1000; i++) {
        CHECK(!g.admit("vd: decoded frame", false, 20.0));
    }
    CHECK(g.admit("an error", true, 20.0));
    CHECK(g.endSession().empty());
    // a clock that goes backwards (another time base after a reset) starts a new window instead of blocking
    mpvlog::Filter h;
    for (int i = 0; i < 100; i++) {
        h.admit("x", true, 50.0);
    }
    CHECK(h.admit("after reset", true, 1.0));
}

TEST(urls_in_third_party_log_lines_keep_only_their_origin) {
    // mpv "Playing: <url>" and FFmpeg "Opening '<url>' for reading": a token in the path no rule knows
    std::string line = redact::urlsToOrigins("Playing: http://cdn.example.com:8080/hls/Zx9QwErTy7Uiop3Asd/index.m3u8?e=1&st=abc");
    CHECK_EQ(line, std::string("Playing: http://cdn.example.com:8080/[...]"));
    line = redact::urlsToOrigins("[hls] Opening 'https://exampleuser:examplepass@seg.example.com/a/b/seg-12.ts' for reading");
    CHECK_EQ(line, std::string("[hls] Opening 'https://seg.example.com/[...]' for reading"));
    // several URLs, a nested one, upper-case scheme, no path
    line = redact::urlsToOrigins("redirect HTTP://A.example.com/x?to=https%3A%2F%2Fb.example.com%2Fy then http://c.example.com");
    CHECK(line.find("/x") == std::string::npos && line.find("b.example.com") == std::string::npos);
    CHECK(line.find("http://A.example.com/[...]") != std::string::npos);
    CHECK(line.find("http://c.example.com") != std::string::npos);
    // text without a URL, a lone "://", a local path and an empty line are unchanged
    CHECK_EQ(redact::urlsToOrigins("first video frame after 1.2s"), std::string("first video frame after 1.2s"));
    CHECK_EQ(redact::urlsToOrigins(":// odd"), std::string(":// odd"));
    CHECK_EQ(redact::urlsToOrigins("Playing: /mnt/usb0/Movies/film.mkv"), std::string("Playing: /mnt/usb0/Movies/film.mkv"));
    CHECK_EQ(redact::urlsToOrigins(""), std::string(""));
    // registered secrets are still masked (host-only credentials)
    redact::addSecret("hostsecret77");
    CHECK(redact::urlsToOrigins("http://hostsecret77.example.com/a").find("hostsecret77") == std::string::npos);
}

TEST(log_file_stops_growing_at_the_hard_cap_even_for_errors) {
    std::string dir = tempDir("log_hardcap");
    diag::init(dir);
    std::string big(1400, 'e');
    for (int i = 0; i < 20000; i++) {   // ~ 28 MB of error lines
        diag::write(diag::Level::Error, "test", big);
    }
    diag::shutdown();
    int64_t size = fs::fileSize(fs::join(dir, "log.txt"));
    CHECK(size > 16L * 1024 * 1024);
    CHECK(size < 25L * 1024 * 1024);
    std::string text = readAll(fs::join(dir, "log.txt"));
    CHECK(text.find("hard log size cap reached") != std::string::npos);
}
