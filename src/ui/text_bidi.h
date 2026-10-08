// Right-to-left lines for the UI text renderer, with GNU FriBidi 1.0.4 (already linked for libass).
//
// A line that contains Arabic / Hebrew is put in visual order (Unicode Bidirectional Algorithm, paragraph
// direction from its first strong character) and Arabic letters are joined into their contextual
// Presentation Forms-B shapes (+ the lam-alef ligatures), which the fallback font (DejaVu Sans) draws. This is
// FriBidi's basic Arabic shaping, not full OpenType shaping (no HarfBuzz in this build): Arabic reads joined
// and in the right order, but mark positioning and the finer typography of a shaping engine are missing.
// Lines without right-to-left characters are never touched.

#ifndef PS4IPTV_UI_TEXT_BIDI_H
#define PS4IPTV_UI_TEXT_BIDI_H

#include <string>

namespace ui {

    // logical -> visual order with Arabic shaping; returns the input unchanged on failure (and on the host
    // test build, which has no FriBidi)
    std::u32string visualLine(const std::u32string &logical);

    // FriBidi is available in this build
    bool bidiAvailable();
}

#endif // PS4IPTV_UI_TEXT_BIDI_H
