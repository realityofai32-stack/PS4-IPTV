// The V7 IPTV colour palette: deep-navy near-black surfaces, white and blue-grey text, and one accent the user
// chooses from a curated set (Settings > Appearance > Accent color). Host-testable: tests/host/test_theme.cpp
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
    constexpr Rgba BACKGROUND = rgb(7, 10, 16);              // app background (top of the gradient)
    constexpr Rgba BACKGROUND_SECONDARY = rgb(12, 16, 26);   // bottom of the background gradient
    constexpr Rgba SURFACE = rgb(18, 24, 36);                // panels, list rows
    constexpr Rgba CARD = rgb(22, 28, 41);                   // tiles, cards
    constexpr Rgba SURFACE_ELEVATED = rgb(31, 39, 56);       // buttons, chips, keys, elevated panels
    constexpr Rgba CARD_HOVER = rgb(42, 52, 73);             // focused card / field (with the accent border)
    constexpr Rgba LOGO_TILE = rgb(46, 54, 72);              // behind channel logos (dark logos stay readable)
    constexpr Rgba DIVIDER = rgb(40, 49, 68);
    constexpr Rgba PANEL = rgb(11, 14, 22, 246);             // side panels (Options, keyboard)

    // ---------------------------------------------------------------- text
    constexpr Rgba TEXT_STRONG = rgb(255, 255, 255);         // focused text, text on dark accents
    constexpr Rgba TEXT_PRIMARY = rgb(236, 239, 245);
    constexpr Rgba TEXT_SECONDARY = rgb(168, 177, 194);
    constexpr Rgba TEXT_MUTED = rgb(124, 134, 152);
    constexpr Rgba TEXT_ON_LIGHT = rgb(8, 11, 18);           // text on bright accents (cyan, green, white...)

    // ---------------------------------------------------------------- accent presets
    // One accent colours the small things: focus borders, the primary button, selected tabs / chips, progress,
    // markers. Backgrounds, posters, video, text bodies and error states never take it.
    struct Accent {
        const char *key;        // settings.json value
        const char *nameKey;    // localization key of its name
        Rgba accent;            // borders, fills, progress
        Rgba text;              // the accent as text (links, eyebrows, brand)
        Rgba muted;             // selected tab / chip at rest
        Rgba focus;             // focused list row: dark, accent-tinted
        Rgba onAccent;          // captions on an accent fill (large semibold text only)
    };

    constexpr Rgba W = TEXT_STRONG;
    constexpr Rgba D = TEXT_ON_LIGHT;

    constexpr Accent ACCENTS[] = {
            {"blue", "appearance.accent.blue", rgb(40, 132, 255), rgb(124, 182, 255), rgb(22, 48, 92), rgb(26, 44, 74), W},
            {"cyan", "appearance.accent.cyan", rgb(0, 190, 228), rgb(104, 218, 242), rgb(8, 58, 74), rgb(14, 52, 64), D},
            {"teal", "appearance.accent.teal", rgb(18, 186, 156), rgb(96, 222, 196), rgb(8, 60, 54), rgb(14, 52, 52), D},
            {"green", "appearance.accent.green", rgb(58, 196, 108), rgb(126, 226, 156), rgb(18, 64, 38), rgb(20, 54, 40), D},
            {"purple", "appearance.accent.purple", rgb(154, 110, 255), rgb(194, 168, 255), rgb(52, 36, 98), rgb(44, 36, 80), W},
            {"magenta", "appearance.accent.magenta", rgb(232, 70, 166), rgb(255, 148, 208), rgb(88, 24, 64), rgb(66, 30, 58), W},
            {"red", "appearance.accent.red", rgb(238, 74, 74), rgb(255, 140, 138), rgb(94, 26, 30), rgb(70, 30, 34), W},
            {"orange", "appearance.accent.orange", rgb(255, 138, 36), rgb(255, 180, 108), rgb(94, 46, 12), rgb(66, 44, 24), D},
            {"white", "appearance.accent.white", rgb(226, 231, 240), rgb(236, 240, 247), rgb(52, 60, 78), rgb(46, 54, 74), D}};

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
    constexpr Rgba OVERLAY = rgb(6, 8, 13, 215);             // playback HUD bars over video
    constexpr Rgba OVERLAY_PANEL = rgb(12, 15, 23, 230);     // playback panels / badges over video
    constexpr Rgba SCRIM = rgb(0, 0, 0, 190);                // behind modal dialogs
    constexpr Rgba VIDEO_BACKGROUND = rgb(0, 0, 0);
    constexpr Rgba PROGRESS_TRACK = rgb(255, 255, 255, 46);
    constexpr Rgba PROGRESS_TRACK_ON_IMAGE = rgb(0, 0, 0, 170);
    constexpr Rgba SCROLL_TRACK = rgb(255, 255, 255, 18);
    constexpr Rgba SCROLL_THUMB = rgb(255, 255, 255, 150);
    constexpr Rgba SCROLL_THUMB_IDLE = rgb(255, 255, 255, 70);
    constexpr Rgba SWITCH_OFF = rgb(62, 72, 92);
    constexpr Rgba SWITCH_KNOB_OFF = rgb(190, 198, 212);
    // backdrop artwork (an enlarged poster) is drawn under these: it stays a dark, quiet wash behind text
    constexpr Rgba BACKDROP_TINT = rgb(150, 156, 170);      // texture modulation: dims the image itself
    constexpr Rgba BACKDROP_SHADE = rgb(7, 10, 16, 150);     // uniform shade over the whole image

    // DualShock symbol colours (the shapes are drawn, the UI font has no PlayStation glyphs)
    constexpr Rgba PAD_CROSS = rgb(125, 170, 240);
    constexpr Rgba PAD_CIRCLE = rgb(240, 110, 120);
    constexpr Rgba PAD_SQUARE = rgb(230, 140, 220);
    constexpr Rgba PAD_TRIANGLE = rgb(80, 210, 180);

    // initials tiles of channels / titles without artwork: dark, desaturated, distinguishable
    constexpr Rgba MONOGRAM[8] = {rgb(44, 66, 104), rgb(82, 56, 104), rgb(36, 88, 84), rgb(104, 70, 44),
                                  rgb(100, 48, 64), rgb(54, 80, 50), rgb(68, 68, 98), rgb(38, 78, 108)};

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
