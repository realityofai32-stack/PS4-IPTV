#include <vector>

#include "text_bidi.h"

#if defined(__PS4__)

#include <fribidi/fribidi.h>

namespace ui {

    bool bidiAvailable() {
        return true;
    }

    std::u32string visualLine(const std::u32string &logical) {
        if (logical.empty() || logical.size() > 4096) {
            return logical;
        }
        auto len = (FriBidiStrIndex) logical.size();
        std::vector<FriBidiChar> text(logical.begin(), logical.end());
        std::vector<FriBidiCharType> types((size_t) len);
        std::vector<FriBidiBracketType> brackets((size_t) len);
        std::vector<FriBidiLevel> levels((size_t) len);
        std::vector<FriBidiArabicProp> joining((size_t) len);

        // the same steps as FriBidi's own fribidi_log2vis (deprecated in 1.0), with Arabic shaping
        fribidi_get_bidi_types(text.data(), len, types.data());
        fribidi_get_bracket_types(text.data(), len, types.data(), brackets.data());
        FriBidiParType base = FRIBIDI_PAR_ON;
        if (fribidi_get_par_embedding_levels_ex(types.data(), brackets.data(), len, &base, levels.data()) == 0) {
            return logical;
        }
        FriBidiFlags flags = FRIBIDI_FLAGS_DEFAULT | FRIBIDI_FLAGS_ARABIC;
        fribidi_get_joining_types(text.data(), len, joining.data());
        fribidi_join_arabic(types.data(), len, levels.data(), joining.data());
        fribidi_shape(flags, levels.data(), len, joining.data(), text.data());
        if (fribidi_reorder_line(flags, types.data(), len, 0, base, levels.data(), text.data(), nullptr) == 0) {
            return logical;
        }
        std::u32string out;
        out.reserve(text.size());
        for (FriBidiChar c: text) {
            // ligatures leave zero-width placeholders behind: not drawn
            if (c != 0xFEFF && c != 0x200B && c != 0) {
                out.push_back((char32_t) c);
            }
        }
        return out;
    }
}

#else

namespace ui {

    bool bidiAvailable() {
        return false;
    }

    std::u32string visualLine(const std::u32string &logical) {
        return logical;
    }
}

#endif
