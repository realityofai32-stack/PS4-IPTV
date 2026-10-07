#include "utf8.h"

namespace utf8 {

    std::u32string decode(const std::string &text) {
        std::u32string out;
        out.reserve(text.size());
        const auto *s = (const unsigned char *) text.data();
        size_t n = text.size();
        size_t i = 0;
        while (i < n) {
            unsigned char c = s[i];
            char32_t cp;
            int extra;
            if (c < 0x80) {
                out.push_back(c);
                i++;
                continue;
            } else if ((c & 0xE0) == 0xC0) {
                cp = c & 0x1F;
                extra = 1;
            } else if ((c & 0xF0) == 0xE0) {
                cp = c & 0x0F;
                extra = 2;
            } else if ((c & 0xF8) == 0xF0) {
                cp = c & 0x07;
                extra = 3;
            } else {
                out.push_back(0xFFFD);
                i++;
                continue;
            }
            bool ok = true;
            for (int k = 1; k <= extra; k++) {
                if (i + (size_t) k >= n || (s[i + k] & 0xC0) != 0x80) {
                    ok = false;
                    break;
                }
                cp = (cp << 6) | (s[i + k] & 0x3F);
            }
            if (!ok) {
                out.push_back(0xFFFD);
                i++;
                continue;
            }
            // reject overlong forms and surrogates
            if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000)
                || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
                cp = 0xFFFD;
            }
            out.push_back(cp);
            i += (size_t) extra + 1;
        }
        return out;
    }

    std::string encode(char32_t cp) {
        std::string out;
        if (cp < 0x80) {
            out += (char) cp;
        } else if (cp < 0x800) {
            out += (char) (0xC0 | (cp >> 6));
            out += (char) (0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char) (0xE0 | (cp >> 12));
            out += (char) (0x80 | ((cp >> 6) & 0x3F));
            out += (char) (0x80 | (cp & 0x3F));
        } else if (cp <= 0x10FFFF) {
            out += (char) (0xF0 | (cp >> 18));
            out += (char) (0x80 | ((cp >> 12) & 0x3F));
            out += (char) (0x80 | ((cp >> 6) & 0x3F));
            out += (char) (0x80 | (cp & 0x3F));
        } else {
            out += "\xEF\xBF\xBD";
        }
        return out;
    }

    void popBack(std::string &text) {
        if (text.empty()) {
            return;
        }
        size_t i = text.size() - 1;
        while (i > 0 && ((unsigned char) text[i] & 0xC0) == 0x80) {
            i--;
        }
        text.erase(i);
    }

    size_t length(const std::string &text) {
        size_t n = 0;
        for (unsigned char c: text) {
            n += (c & 0xC0) != 0x80;
        }
        return n;
    }

    namespace {
        // Latin-1 Supplement U+00C0..U+00FF -> ASCII base letter (0 = keep)
        const char LATIN1[64] = {
                'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',   // C0
                'd', 'n', 'o', 'o', 'o', 'o', 'o', 0, 'o', 'u', 'u', 'u', 'u', 'y', 't', 's',     // D0 (D7 ×)
                'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',   // E0
                'd', 'n', 'o', 'o', 'o', 'o', 'o', 0, 'o', 'u', 'u', 'u', 'u', 'y', 't', 'y'};    // F0 (F7 ÷)

        // Latin Extended-A U+0100..U+017F as [first, last] ranges -> ASCII base letter
        struct Range {
            char32_t first;
            char32_t last;
            char base;
        };
        const Range LATIN_EXT_A[] = {
                {0x100, 0x105, 'a'}, {0x106, 0x10D, 'c'}, {0x10E, 0x111, 'd'}, {0x112, 0x11B, 'e'},
                {0x11C, 0x123, 'g'}, {0x124, 0x127, 'h'}, {0x128, 0x133, 'i'}, {0x134, 0x135, 'j'},
                {0x136, 0x138, 'k'}, {0x139, 0x142, 'l'}, {0x143, 0x14B, 'n'}, {0x14C, 0x153, 'o'},
                {0x154, 0x159, 'r'}, {0x15A, 0x161, 's'}, {0x162, 0x167, 't'}, {0x168, 0x173, 'u'},
                {0x174, 0x175, 'w'}, {0x176, 0x178, 'y'}, {0x179, 0x17E, 'z'}, {0x17F, 0x17F, 's'}};
    }

    std::u32string foldForSearch(const std::string &text) {
        // case-insensitive and accent-insensitive: the on-screen keyboard is ASCII, so "sehir" must find
        // "Şehir" and "istanbul" must find "İstanbul"
        std::u32string s = decode(text);
        for (auto &c: s) {
            if (c >= 'A' && c <= 'Z') {
                c += 32;
            } else if (c >= 0xC0 && c <= 0xFF) {
                char b = LATIN1[c - 0xC0];
                if (b) {
                    c = (char32_t) b;
                }
            } else if (c >= 0x100 && c <= 0x17F) {
                for (const auto &r: LATIN_EXT_A) {
                    if (c >= r.first && c <= r.last) {
                        c = (char32_t) r.base;
                        break;
                    }
                }
            } else if (c >= 0x410 && c <= 0x42F) {
                c += 0x20;  // Cyrillic upper -> lower
            } else if (c >= 0x400 && c <= 0x40F) {
                c += 0x50;
            } else if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) {
                c += 0x20;  // Greek upper -> lower
            }
        }
        return s;
    }
}
