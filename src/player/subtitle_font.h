// The font libass draws text subtitles with (host-testable).
//
// mpv 0.34.1 hands libass <config-dir>/subfont.ttf as its default font. The PS4's libass 0.14.0 has no font provider
// (no fontconfig), so that one file must already contain every glyph: a glyph it lacks makes libass search again for
// every glyph of every frame (GitHub issue #1, "arabic subtitles lag"). The packaged V7Subtitles.ttf is Inter
// SemiBold (unchanged) plus Arabic, Persian and Hebrew (third_party/fonts-subtitles). Older packages without it fall
// back to Inter SemiBold, as before.

#ifndef PS4IPTV_PLAYER_SUBTITLE_FONT_H
#define PS4IPTV_PLAYER_SUBTITLE_FONT_H

#include <string>

namespace subfont {

    struct Result {
        bool installed = false;   // copied now (first start, or the packaged font changed)
        bool ok = false;          // <mpvDir>/subfont.ttf is the current font (copied now or before)
        std::string source;       // the packaged file used (file name only)
        std::string error;
    };

    // Copies the packaged subtitle font from <fontsDir> to <mpvDir>/subfont.ttf when it is missing or differs from
    // the packaged one (a marker file records which font / size / version was copied, so a replaced font of the
    // same size is copied too). `version` is the app version (a new version re-checks once).
    Result install(const std::string &fontsDir, const std::string &mpvDir, const std::string &version);
}

#endif // PS4IPTV_PLAYER_SUBTITLE_FONT_H
