// Font choice per code point (host-testable).
//
// The UI font (Inter: Latin incl. Turkish, Greek, Cyrillic) draws everything it has. A code point it lacks is
// drawn with the first fallback font that has it (DejaVu Sans: Arabic, Hebrew, Armenian, Georgian, U+FFFD...;
// Droid Sans Fallback: kana, Hangul, CJK). A code point no font has is drawn with the UI font, which shows its
// missing-glyph box (".notdef"): never a crash, never a glyph taken from the wrong place.
//
// Coverage is asked once per code point (FreeType FT_Get_Char_Index on the real faces) and cached.

#ifndef PS4IPTV_UI_FONT_FALLBACK_H
#define PS4IPTV_UI_FONT_FALLBACK_H

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

    class FontChooser {

    public:

        // has(font, codePoint): the font has a glyph for it. coverageFont: a font with the UI font's coverage
        // (both weights of Inter cover the same code points). fallbacks: tried in order.
        using Has = std::function<bool(int font, char32_t codePoint)>;

        FontChooser() = default;

        FontChooser(int coverageFont, std::vector<int> fallbacks, Has has)
                : coverage(coverageFont), order(std::move(fallbacks)), has(std::move(has)) {}

        // the font to draw `cp` with, for a label whose UI font (weight) is `primary`
        int pick(int primary, char32_t cp) {
            if (cp < 0x0250 || !has) {
                return primary;   // Latin (incl. Turkish) and controls: always the UI font, no lookup
            }
            auto it = cache.find(cp);
            if (it == cache.end()) {
                int chosen = -1;
                if (!has(coverage, cp)) {
                    for (int f: order) {
                        if (has(f, cp)) {
                            chosen = f;
                            break;
                        }
                    }
                }
                it = cache.emplace(cp, chosen).first;
                if (chosen < 0 && !has(coverage, cp)) {
                    missing++;
                }
            }
            return it->second < 0 ? primary : it->second;
        }

        // code points looked up / with no glyph in any font (shown as the missing-glyph box)
        int lookups() const { return (int) cache.size(); }

        int missingGlyphs() const { return missing; }

    private:

        int coverage = 0;
        std::vector<int> order;
        Has has;
        std::unordered_map<char32_t, int> cache;   // -1 = the UI font
        int missing = 0;
    };

    // right-to-left text (Hebrew, Arabic, Syriac, Thaana, NKo and their presentation forms): such lines go
    // through the bidi / Arabic shaping step before they are laid out
    inline bool isRtl(char32_t c) {
        return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF);
    }

    inline bool hasRtl(const std::u32string &s) {
        for (char32_t c: s) {
            if (isRtl(c)) {
                return true;
            }
        }
        return false;
    }
}

#endif // PS4IPTV_UI_FONT_FALLBACK_H
