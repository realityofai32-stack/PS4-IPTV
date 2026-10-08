// ui::GlyphCache against a model of libcross2d's PS4 font page texture (GLTexture, GLES2):
//  - Font::loadGlyph writes glyph pixels only into the CPU copy of the page (GLTexture::lock)
//  - the GPU copy changes only on unlock() (full glTexSubImage2D) or when the page grows (resize keeps
//    the CPU pixels and re-uploads all of them with glTexImage2D; the new area is uninitialised)
//  - pages start at 128x128 with uninitialised memory and double up to 1024x1024
// "Drawing" a glyph reads the GPU copy, so a glyph that was never uploaded shows up as a mismatch -
// the missing/garbled letters seen on the PS4 at hardware Checkpoint 1.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/core/utf8.h"
#include "../../src/ui/glyph_cache.h"

namespace {

    const uint32_t GARBAGE = 0xCDCDCDCDu;

    struct FakeGlyph {
        int x = 0, y = 0, w = 0, h = 0;
        float advance = 0;
    };

    struct FakePage {
        int w = 128, h = 128;
        std::vector<uint32_t> cpu, gpu;
        int rowX = 0, rowY = 3, rowH = 0;  // libcross2d starts rows at y = 3 (2x2 white square)
        bool cleared = false;

        FakePage() : cpu((size_t) w * h, GARBAGE), gpu((size_t) w * h, GARBAGE) {}

        void grow() {
            int nw = w * 2, nh = h * 2;
            std::vector<uint32_t> n((size_t) nw * nh, GARBAGE);
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    n[(size_t) y * nw + x] = cpu[(size_t) y * w + x];
                }
            }
            cpu = n;
            gpu = n;  // glTexImage2D(..., pixels): everything in the CPU copy reaches the GPU
            w = nw;
            h = nh;
        }
    };

    uint32_t signature(int font, unsigned size, char32_t cp) {
        return 0x01000000u ^ ((uint32_t) font << 30) ^ (size << 21) ^ (uint32_t) cp;
    }

    struct FakeBackend {
        using Glyph = FakeGlyph;

        std::map<std::pair<int, unsigned>, FakePage> pages;
        std::vector<std::string> events;
        bool loadIntoUnpreparedPage = false;

        FakePage &page(int font, unsigned size) {
            return pages[{font, size}];
        }

        void preparePage(int font, unsigned size) {
            FakePage &p = page(font, size);
            std::fill(p.cpu.begin(), p.cpu.end(), 0u);
            p.cleared = true;
        }

        Glyph load(int font, unsigned size, char32_t cp) {
            FakePage &p = page(font, size);
            if (!p.cleared) {
                loadIntoUnpreparedPage = true;
            }
            Glyph g;
            g.advance = (float) size * 0.6f;
            if (cp == U' ') {
                return g;  // no bitmap
            }
            int gw = (int) size / 2 + (int) (cp % 7), gh = (int) size + 2;
            int pw = gw + 2, ph = gh + 2;  // 1 px transparent padding like libcross2d
            if (p.rowX + pw > p.w) {
                p.rowY += p.rowH;
                p.rowX = 0;
                p.rowH = 0;
            }
            while (p.rowY + ph >= p.h || pw >= p.w) {
                if (p.w * 2 > 1024) {
                    return g;  // full: libcross2d hands out an empty rect
                }
                p.grow();
            }
            for (int y = 0; y < ph; y++) {
                for (int x = 0; x < pw; x++) {
                    bool inside = x > 0 && y > 0 && x < pw - 1 && y < ph - 1;
                    p.cpu[(size_t) (p.rowY + y) * p.w + p.rowX + x] = inside ? signature(font, size, cp) : 0u;
                }
            }
            g.x = p.rowX + 1;
            g.y = p.rowY + 1;
            g.w = gw;
            g.h = gh;
            p.rowX += pw;
            p.rowH = std::max(p.rowH, ph);
            return g;
        }

        void upload(int font, unsigned size) {
            FakePage &p = page(font, size);
            p.gpu = p.cpu;
            events.push_back("upload");
        }

        std::pair<int, int> pageSize(int font, unsigned size) {
            FakePage &p = page(font, size);
            return {p.w, p.h};
        }

        // what the GPU would sample for this glyph: true when every texel is the glyph's own pixel
        bool drawsCorrectly(int font, unsigned size, char32_t cp, const FakeGlyph &g) {
            FakePage &p = page(font, size);
            for (int y = g.y; y < g.y + g.h; y++) {
                for (int x = g.x; x < g.x + g.w; x++) {
                    if (p.gpu[(size_t) y * p.w + x] != signature(font, size, cp)) {
                        return false;
                    }
                }
            }
            return true;
        }
    };

    struct FakeLabel {
        int font;
        unsigned size;
        std::u32string text;
    };

    // the strings of the hardware text test screen, in its sizes and weights
    std::vector<FakeLabel> screenLabels() {
        const char *strings[] = {
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz", "0123456789  :/._-@?=&%#+!",
                "\xC3\x87\xC3\xA7 \xC4\x9E\xC4\x9F \xC4\xB0i I\xC4\xB1 \xC3\x96\xC3\xB6 \xC5\x9E\xC5\x9F \xC3\x9C\xC3\xBC",
                "Add Xtream Profile", "http://example.com:8080", "Username required", "Password required",
                "Fill in the fields, then use Test Connection.", "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
        };
        const unsigned sizes[] = {18, 20, 22, 24, 26, 28, 34, 44, 64};
        std::vector<FakeLabel> labels;
        for (unsigned size: sizes) {
            for (int font = 0; font < 2; font++) {
                for (const char *s: strings) {
                    labels.push_back({font, size, utf8::decode(s)});
                }
            }
        }
        return labels;
    }

    // frames: some labels are (re)built, then everything built so far is drawn; returns wrong glyph draws
    int simulate(bool flushBeforeDraw, FakeBackend &backend, ui::GlyphCache<FakeBackend> &cache) {
        std::vector<FakeLabel> labels = screenLabels();
        std::vector<FakeLabel> built;
        int wrong = 0;
        for (size_t i = 0; i < labels.size(); i++) {
            built.push_back(labels[i]);
            for (char32_t cp: labels[i].text) {
                cache.get(labels[i].font, labels[i].size, cp);
            }
            if (i % 5 != 4 && i + 1 != labels.size()) {
                continue;  // several labels per frame
            }
            if (flushBeforeDraw) {
                cache.flush();
            }
            for (const auto &l: built) {
                for (char32_t cp: l.text) {
                    const FakeGlyph &g = cache.get(l.font, l.size, cp);
                    if (g.w > 0 && !backend.drawsCorrectly(l.font, l.size, cp, g)) {
                        wrong++;
                    }
                }
            }
        }
        return wrong;
    }
}

TEST(text_atlas_model_reproduces_the_hardware_bug) {
    // negative control: without uploads (the Checkpoint 1 Label), glyphs added after a page's last growth
    // are drawn from stale GPU memory
    FakeBackend backend;
    ui::GlyphCache<FakeBackend> cache(backend);
    CHECK(simulate(false, backend, cache) > 0);
}

TEST(text_atlas_every_drawn_glyph_is_uploaded) {
    FakeBackend backend;
    ui::GlyphCache<FakeBackend> cache(backend);
    CHECK_EQ(simulate(true, backend, cache), 0);
    CHECK(!backend.loadIntoUnpreparedPage);
    CHECK(!cache.pending());
    CHECK_EQ(cache.flush(), 0);  // nothing new: no upload
    auto s = cache.getStats();
    CHECK_EQ(s.pages, 18);       // 9 sizes x 2 weights
    CHECK(s.resizes > 0);        // the test really exercised page growth
}

TEST(text_atlas_glyphs_added_after_first_draw) {
    FakeBackend backend;
    ui::GlyphCache<FakeBackend> cache(backend);
    std::u32string reveal = utf8::decode("\xC3\x87\xC4\x9E\xC4\xB0I\xC3\x96\xC5\x9E\xC3\x9C \xC3\xA7\xC4\x9Fi\xC4\xB1"
                                         "\xC3\xB6\xC5\x9F\xC3\xBC AQWXYZ aqwxyz 0123456789");
    int wrong = 0;
    for (size_t n = 1; n <= reveal.size(); n++) {  // one more character per frame
        for (size_t i = 0; i < n; i++) {
            cache.get(1, 30, reveal[i]);
        }
        cache.flush();
        for (size_t i = 0; i < n; i++) {
            const FakeGlyph &g = cache.get(1, 30, reveal[i]);
            if (g.w > 0 && !backend.drawsCorrectly(1, 30, reveal[i], g)) {
                wrong++;
            }
        }
    }
    CHECK_EQ(wrong, 0);
}

TEST(text_atlas_generation_follows_page_growth) {
    FakeBackend backend;
    ui::GlyphCache<FakeBackend> cache(backend);
    CHECK_EQ(cache.generation(0, 64), 0u);  // unknown page
    cache.get(0, 64, U'A');
    unsigned g0 = cache.generation(0, 64);
    CHECK(g0 != 0);
    std::pair<int, int> size0 = backend.pageSize(0, 64);
    char32_t cp = U'B';
    while (backend.pageSize(0, 64) == size0) {
        cache.get(0, 64, cp++);
    }
    CHECK(cache.generation(0, 64) != g0);
    unsigned g1 = cache.generation(0, 64);
    cache.get(0, 64, U'A');                 // cached: no change
    CHECK_EQ(cache.generation(0, 64), g1);
    CHECK_EQ(cache.generation(1, 64), 0u);  // other weight is a separate page
}

TEST(text_atlas_keys_and_reference_stability) {
    FakeBackend backend;
    ui::GlyphCache<FakeBackend> cache(backend);
    const FakeGlyph *first = &cache.get(0, 24, U'Ş');  // Ş
    FakeGlyph copy = *first;
    for (char32_t cp = 0x20; cp < 0x20 + 3000; cp++) {      // forces rehashing of the glyph map
        cache.get(0, 18, cp);
    }
    CHECK(first == &cache.get(0, 24, U'Ş'));
    CHECK(first->x == copy.x && first->y == copy.y && first->w == copy.w);
    // same code point in another size, weight, or a code point above 0xFFFF are distinct glyphs
    const FakeGlyph &a = cache.get(0, 28, U'A');
    const FakeGlyph &b = cache.get(1, 28, U'A');
    const FakeGlyph &c = cache.get(0, 29, U'A');
    CHECK(&a != &b && &a != &c && &b != &c);
    CHECK(&cache.get(0, 28, (char32_t) 0x1F4FA) != &cache.get(0, 28, (char32_t) 0xF4FA));
}

TEST(text_test_screen_strings_decode) {
    // "Çç Ğğ İi Iı Öö Şş Üü"
    std::u32string d = utf8::decode("\xC3\x87\xC3\xA7 \xC4\x9E\xC4\x9F \xC4\xB0i I\xC4\xB1 \xC3\x96\xC3\xB6 "
                                    "\xC5\x9E\xC5\x9F \xC3\x9C\xC3\xBC");
    std::u32string expected = {0xC7, 0xE7, ' ', 0x11E, 0x11F, ' ', 0x130, 'i', ' ', 'I', 0x131, ' ', 0xD6, 0xF6,
                               ' ', 0x15E, 0x15F, ' ', 0xDC, 0xFC};
    CHECK(d == expected);
    // the split literals of the reveal line ("\xC5\x9E" "ehir") must not swallow the following letter
    std::u32string s = utf8::decode("\xC5\x9E" "ehir Kanal\xC4\xB1");
    CHECK(s.size() == 12 && s[0] == 0x15E && s[1] == 'e' && s[11] == 0x131);
}

// ------------------------------------------------------------------ font fallback (ui/font_fallback.h)
#include "../../src/ui/font_fallback.h"

TEST(font_fallback_choice_per_code_point) {
    // fonts: 0 Inter Regular, 1 Inter SemiBold, 2 scripts fallback, 3 CJK fallback
    auto inRange = [](char32_t c, char32_t a, char32_t b) { return c >= a && c <= b; };
    int asked = 0;
    ui::FontChooser chooser(0, {2, 3}, [&](int font, char32_t c) {
        asked++;
        if (font == 0) {
            return c < 0x0250 || inRange(c, 0x0370, 0x04FF);              // Latin, Greek, Cyrillic
        }
        if (font == 2) {
            return inRange(c, 0x0590, 0x06FF) || inRange(c, 0xFE70, 0xFEFF) || c == 0xFFFD;
        }
        return inRange(c, 0x3040, 0x30FF) || inRange(c, 0xAC00, 0xD7A3) || inRange(c, 0x4E00, 0x9FFF);
    });
    // Latin / Turkish: the label's own weight, without any lookup
    CHECK_EQ(chooser.pick(1, U'\x15F'), 1);   // ş
    CHECK_EQ(chooser.pick(0, U'A'), 0);
    CHECK_EQ(asked, 0);
    // Greek / Cyrillic: the UI font (both weights)
    CHECK_EQ(chooser.pick(1, 0x0416), 1);      // Ж
    CHECK_EQ(chooser.pick(0, 0x03A9), 0);      // Ω
    // other scripts: the first fallback that has them, whatever the weight
    CHECK_EQ(chooser.pick(0, 0x0627), 2);      // Arabic alef
    CHECK_EQ(chooser.pick(1, 0x05D0), 2);      // Hebrew alef
    CHECK_EQ(chooser.pick(1, 0xFEFB), 2);      // lam-alef ligature (presentation form)
    CHECK_EQ(chooser.pick(0, 0x3042), 3);      // Hiragana a
    CHECK_EQ(chooser.pick(1, 0xD55C), 3);      // Hangul han
    CHECK_EQ(chooser.pick(0, 0x65E5), 3);      // CJK 日
    CHECK_EQ(chooser.pick(0, 0xFFFD), 2);      // replacement character
    // no font has it: the UI font (its missing-glyph box), counted
    CHECK_EQ(chooser.pick(1, 0x0E01), 1);      // Thai
    CHECK_EQ(chooser.missingGlyphs(), 1);
    // cached: asking again does not query the fonts
    int before = asked;
    CHECK_EQ(chooser.pick(0, 0x65E5), 3);
    CHECK_EQ(chooser.pick(0, 0x0E01), 0);
    CHECK_EQ(asked, before);
    CHECK_EQ(chooser.missingGlyphs(), 1);
    // without coverage data (fallback fonts missing): always the UI font, as before Checkpoint 3.2
    ui::FontChooser none;
    CHECK_EQ(none.pick(1, 0x65E5), 1);
}

TEST(font_fallback_right_to_left_detection) {
    CHECK(!ui::hasRtl(U"TRT 1 HD"));
    CHECK(!ui::hasRtl(U"\x041F\x0435\x0440\x0432\x044B\x0439"));   // Cyrillic
    CHECK(!ui::hasRtl(U"NHK \x7DCF\x5408"));                         // CJK
    CHECK(ui::hasRtl(U"Al \x062C\x0632\x064A\x0631\x0629"));       // Arabic
    CHECK(ui::hasRtl(U"\x05E2\x05E8\x05D5\x05E5 11"));               // Hebrew
    CHECK(ui::hasRtl(U"\xFEFB"));                                    // presentation form
    CHECK(ui::isRtl(0x0600) && ui::isRtl(0x08FF) && !ui::isRtl(0x0900));
}
