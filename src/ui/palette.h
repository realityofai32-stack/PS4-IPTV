// The V7 IPTV colour palette: a neutral black / charcoal base (no navy cast), white and neutral-grey text, and one
// accent the user chooses from a curated set (Settings > Appearance > Accent color). The base never changes with
// the accent: only the accent tokens (and the barely tinted focus surfaces derived from it) do. Host-testable: tests/host/test_theme.cpp
// checks text contrast on every surface for every accent preset.
// Screens never use these values directly: they use the theme:: tokens (ui/theme.h).

#ifndef PS4IPTV_UI_PALETTE_H
#define PS4IPTV_UI_PALETTE_H

#include <cmath>
#include <cstdint>
#include <cstring>

namespace palette {

    struct Rgba {
        uint8_t r, g, b, a;
    };

    constexpr Rgba rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }

    // ---------------------------------------------------------------- surfaces (darkest to lightest)
    // Professional black: every step is a neutral charcoal with at most a faint cool cast (blue - red <= 13).
    // Surfaces start at ~19 so they stay distinct from the page on TVs fed limited-range RGB (0-16 crush to black).
    constexpr Rgba BACKGROUND = rgb(5, 6, 8);                // app background (top of the gradient), #050608
    constexpr Rgba BACKGROUND_SECONDARY = rgb(8, 9, 12);     // bottom of the background gradient, bands
    constexpr Rgba SURFACE = rgb(19, 21, 25);                // panels, list rows
    constexpr Rgba CARD = rgb(22, 25, 30);                   // tiles, cards
    constexpr Rgba SURFACE_ELEVATED = rgb(29, 33, 39);       // buttons, chips, keys, elevated panels
    constexpr Rgba CARD_HOVER = rgb(45, 49, 57);             // focused card / field (with the accent border)
    constexpr Rgba LOGO_TILE = rgb(47, 51, 58);              // behind channel logos (dark logos stay readable)
    constexpr Rgba DIVIDER = rgb(38, 43, 51);
    constexpr Rgba DIVIDER_SOFT = rgb(20, 22, 27);           // 1 px separators between list rows on the background
    constexpr Rgba PANEL = rgb(8, 9, 12, 246);               // side panels (Options, keyboard)
    // the raised neutral surface of a focused row / cell, before its faint accent tint (see Accent::focus)
    constexpr Rgba FOCUS_NEUTRAL = rgb(34, 38, 45);

    // ---------------------------------------------------------------- text
    constexpr Rgba TEXT_STRONG = rgb(255, 255, 255);         // focused text, text on dark accents
    constexpr Rgba TEXT_PRIMARY = rgb(245, 247, 250);
    constexpr Rgba TEXT_SECONDARY = rgb(165, 173, 185);
    constexpr Rgba TEXT_MUTED = rgb(122, 131, 144);
    constexpr Rgba TEXT_ON_LIGHT = rgb(6, 7, 9);             // text on bright accents (cyan, green, white...)

    // ---------------------------------------------------------------- accent presets
    // One accent colours the small things: focus edges and rings, the primary button, selected tabs / chips,
    // progress, toggles, markers. Backgrounds, large surfaces, posters, video, text bodies and error states never
    // take it. The two derived surfaces stay neutral: focus = FOCUS_NEUTRAL + 4.5 % accent, muted = (38, 42, 49)
    // + 10 % accent (tests/host/test_theme.cpp keeps them within a few levels of the neutral base).
    struct Accent {
        const char *key;        // settings.json value
        const char *nameKey;    // localization key of its name
        Rgba accent;            // edges, rings, fills of small controls, progress
        Rgba text;              // the accent as text (links, eyebrows, brand)
        Rgba muted;             // selected tab / chip at rest: neutral, faintly tinted
        Rgba focus;             // focused list row / cell: raised neutral, faintly tinted (the accent edge marks it)
        Rgba onAccent;          // captions on an accent fill (large semibold text only)
    };

    constexpr Rgba W = TEXT_STRONG;
    constexpr Rgba D = TEXT_ON_LIGHT;

    constexpr Accent ACCENTS[] = {
            {"blue", "appearance.accent.blue", rgb(40, 132, 255), rgb(124, 182, 255), rgb(38, 51, 70), rgb(34, 42, 54), W},
            {"cyan", "appearance.accent.cyan", rgb(0, 190, 228), rgb(104, 218, 242), rgb(34, 57, 67), rgb(32, 45, 53), D},
            {"teal", "appearance.accent.teal", rgb(18, 186, 156), rgb(96, 222, 196), rgb(36, 56, 60), rgb(33, 45, 50), D},
            {"green", "appearance.accent.green", rgb(58, 196, 108), rgb(126, 226, 156), rgb(40, 57, 55), rgb(35, 45, 48), D},
            {"purple", "appearance.accent.purple", rgb(154, 110, 255), rgb(194, 168, 255), rgb(50, 49, 70), rgb(39, 41, 54), W},
            {"magenta", "appearance.accent.magenta", rgb(232, 70, 166), rgb(255, 148, 208), rgb(57, 45, 61), rgb(43, 39, 50), W},
            {"red", "appearance.accent.red", rgb(238, 74, 74), rgb(255, 140, 138), rgb(58, 45, 52), rgb(43, 40, 46), W},
            {"orange", "appearance.accent.orange", rgb(255, 138, 36), rgb(255, 180, 108), rgb(60, 52, 48), rgb(44, 42, 45), D},
            {"white", "appearance.accent.white", rgb(226, 231, 240), rgb(236, 240, 247), rgb(57, 61, 68), rgb(43, 47, 54), D}};

    constexpr int ACCENT_COUNT = (int) (sizeof(ACCENTS) / sizeof(ACCENTS[0]));
    constexpr int DEFAULT_ACCENT = 0;   // V7 Blue

    // index of a settings key, -1 when unknown
    inline int accentIndex(const char *key) {
        for (int i = 0; i < ACCENT_COUNT; i++) {
            if (key && std::strcmp(ACCENTS[i].key, key) == 0) {
                return i;
            }
        }
        return -1;
    }

    // ---------------------------------------------------------------- states
    constexpr Rgba SUCCESS = rgb(64, 200, 130);
    constexpr Rgba WARNING = rgb(244, 182, 66);
    constexpr Rgba DANGER = rgb(248, 112, 116);
    constexpr Rgba SUCCESS_SURFACE = rgb(20, 72, 52);        // success toast
    constexpr Rgba DANGER_SURFACE = rgb(104, 30, 40);        // error toast

    // ---------------------------------------------------------------- overlays
    constexpr Rgba OVERLAY = rgb(4, 5, 7, 215);              // playback HUD bars over video (neutral black)
    constexpr Rgba OVERLAY_PANEL = rgb(9, 10, 13, 230);      // playback panels / badges over video
    constexpr Rgba SCRIM = rgb(0, 0, 0, 190);                // behind modal dialogs
    constexpr Rgba VIDEO_BACKGROUND = rgb(0, 0, 0);
    constexpr Rgba PROGRESS_TRACK = rgb(255, 255, 255, 46);
    constexpr Rgba PROGRESS_TRACK_ON_IMAGE = rgb(0, 0, 0, 170);
    constexpr Rgba SCROLL_TRACK = rgb(255, 255, 255, 10);
    constexpr Rgba SCROLL_THUMB = rgb(255, 255, 255, 120);
    constexpr Rgba SCROLL_THUMB_IDLE = rgb(255, 255, 255, 44);
    constexpr Rgba SWITCH_OFF = rgb(60, 65, 74);
    constexpr Rgba SWITCH_KNOB_OFF = rgb(192, 197, 206);
    // backdrop artwork is drawn under these: it stays a dark, quiet presence behind the text (neutral: the
    // artwork's own colours are kept, only darker)
    constexpr Rgba BACKDROP_TINT = rgb(150, 150, 152);      // texture modulation: dims the image itself
    constexpr Rgba BACKDROP_SHADE = rgb(5, 6, 8, 150);       // uniform shade over the whole image
    // texture modulation of artwork that is not focused (posters, cards): the focused one is drawn at 100 %
    constexpr Rgba ARTWORK_DIM = rgb(206, 209, 216);

    // DualShock symbol colours (the shapes are drawn, the UI font has no PlayStation glyphs)
    constexpr Rgba PAD_CROSS = rgb(125, 170, 240);
    constexpr Rgba PAD_CIRCLE = rgb(240, 110, 120);
    constexpr Rgba PAD_SQUARE = rgb(230, 140, 220);
    constexpr Rgba PAD_TRIANGLE = rgb(80, 210, 180);

    // initials tiles of channels / titles without artwork: dark, desaturated (35 % towards neutral), distinguishable
    constexpr Rgba MONOGRAM[8] = {rgb(43, 58, 85), rgb(67, 52, 85), rgb(37, 73, 72), rgb(82, 61, 46),
                                  rgb(79, 47, 59), rgb(49, 67, 50), rgb(58, 60, 81), rgb(39, 66, 88)};

    // ---------------------------------------------------------------- contrast (WCAG 2.x)
    // `fg` composited over an opaque `bg`
    inline Rgba over(Rgba fg, Rgba bg) {
        double a = fg.a / 255.0;
        auto mix = [a](uint8_t f, uint8_t b) { return (uint8_t) std::lround(f * a + b * (1 - a)); };
        return rgb(mix(fg.r, bg.r), mix(fg.g, bg.g), mix(fg.b, bg.b));
    }

    inline double luminance(Rgba c) {
        auto lin = [](uint8_t v) {
            double s = v / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
    }

    inline double contrast(Rgba a, Rgba b) {
        double la = luminance(a);
        double lb = luminance(b);
        return (std::fmax(la, lb) + 0.05) / (std::fmin(la, lb) + 0.05);
    }
}

#endif // PS4IPTV_UI_PALETTE_H
