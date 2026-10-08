// The one PS4 IPTV colour palette: near-black / charcoal surfaces, white and soft-grey text, one restrained
// cyan-blue accent (host-testable: tests/host/test_theme.cpp checks text contrast on every surface).
// Screens never use these values directly: they use the theme:: tokens (ui/theme.h).

#ifndef PS4IPTV_UI_PALETTE_H
#define PS4IPTV_UI_PALETTE_H

#include <cmath>
#include <cstdint>

namespace palette {

    struct Rgba {
        uint8_t r, g, b, a;
    };

    constexpr Rgba rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return {r, g, b, a}; }

    // ---------------------------------------------------------------- surfaces (darkest to lightest)
    constexpr Rgba BACKGROUND = rgb(10, 11, 13);             // app background (top of the gradient)
    constexpr Rgba BACKGROUND_SECONDARY = rgb(17, 18, 21);   // bottom of the background gradient
    constexpr Rgba SURFACE = rgb(22, 24, 28);                // panels, list rows
    constexpr Rgba CARD = rgb(27, 29, 34);                   // tiles, cards
    constexpr Rgba SURFACE_ELEVATED = rgb(36, 39, 45);       // buttons, chips, keys, elevated panels
    constexpr Rgba CARD_HOVER = rgb(46, 50, 58);             // focused card / field (with the accent border)
    constexpr Rgba FOCUS = rgb(34, 46, 62);                  // focused list row: dark, slightly accent-tinted
    constexpr Rgba LOGO_TILE = rgb(44, 47, 54);              // behind channel logos (dark logos stay readable)
    constexpr Rgba DIVIDER = rgb(44, 47, 53);
    constexpr Rgba PANEL = rgb(16, 17, 20, 246);             // side panels (Options, keyboard)

    // ---------------------------------------------------------------- text
    constexpr Rgba TEXT_STRONG = rgb(255, 255, 255);         // focused text, text on the accent
    constexpr Rgba TEXT_PRIMARY = rgb(236, 238, 242);
    constexpr Rgba TEXT_SECONDARY = rgb(166, 172, 182);
    constexpr Rgba TEXT_MUTED = rgb(122, 128, 139);

    // ---------------------------------------------------------------- accent and states
    constexpr Rgba ACCENT = rgb(46, 132, 234);               // focus borders, markers, progress, focused button
    constexpr Rgba ACCENT_TEXT = rgb(120, 184, 255);         // the accent as text (links, "+ Add", brand name)
    constexpr Rgba ACCENT_MUTED = rgb(30, 64, 104);          // primary button / selected chip at rest
    constexpr Rgba SUCCESS = rgb(64, 196, 128);
    constexpr Rgba WARNING = rgb(242, 178, 62);
    constexpr Rgba DANGER = rgb(246, 108, 112);
    constexpr Rgba SUCCESS_SURFACE = rgb(22, 74, 52);        // success toast
    constexpr Rgba DANGER_SURFACE = rgb(104, 32, 38);        // error toast

    // ---------------------------------------------------------------- overlays
    constexpr Rgba OVERLAY = rgb(8, 9, 11, 215);             // playback HUD bars over video
    constexpr Rgba OVERLAY_PANEL = rgb(14, 15, 18, 228);     // playback panels / badges over video
    constexpr Rgba SCRIM = rgb(0, 0, 0, 190);                // behind modal dialogs
    constexpr Rgba VIDEO_BACKGROUND = rgb(0, 0, 0);
    constexpr Rgba PROGRESS_TRACK = rgb(255, 255, 255, 46);
    constexpr Rgba PROGRESS_TRACK_ON_IMAGE = rgb(0, 0, 0, 170);
    constexpr Rgba SCROLL_TRACK = rgb(255, 255, 255, 18);
    constexpr Rgba SCROLL_THUMB = rgb(255, 255, 255, 150);
    constexpr Rgba SCROLL_THUMB_IDLE = rgb(255, 255, 255, 70);
    constexpr Rgba SWITCH_OFF = rgb(64, 69, 79);
    constexpr Rgba SWITCH_KNOB_OFF = rgb(190, 196, 206);

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
