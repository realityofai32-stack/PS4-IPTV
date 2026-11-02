// Visual design tokens of the V7 IPTV 1920x1080 TV layout: one deep-navy dark theme with a user-chosen accent.
//
// Every screen colour comes from here (tests/host/test_theme.cpp rejects colour literals in screens and
// widgets). The values live in ui/palette.h, where the host tests check text contrast on every surface for
// every accent preset.
//
// Hierarchy: background (near-black gradient) < surface (panels, rows) < card (tiles) < surfaceElevated
// (buttons, chips) < cardHover (focused card). Focus = one step lighter fill + an accent outline; the accent
// fills only small things (the primary button, selected chips, markers, progress).
//
// The accent is chosen at run time (Settings > Appearance > Accent color): accent(), accentText(),
// accentMuted(), focus() and onAccent() read the active preset. Screens read colours when they build shapes or
// bind rows, so a new accent is applied by rebuilding the screens (App::applyAccent).

#ifndef PS4IPTV_UI_THEME_H
#define PS4IPTV_UI_THEME_H

#include "cross2d/c2d.h"
#include "palette.h"

namespace theme {

    // ---------------------------------------------------------------- layout
    const float SCREEN_W = 1920;
    const float SCREEN_H = 1080;
    const float SAFE_X = 96;        // ~5% TV-safe area: no text, navigation or hints outside it
    const float SAFE_Y = 54;
    const float CONTENT_W = SCREEN_W - 2 * SAFE_X;
    const float TOPBAR_H = 108;
    const float HINTS_Y = SCREEN_H - SAFE_Y - 40;   // top of the controller hint bar

    // spacing scale
    const float SPACE_XS = 8;
    const float SPACE_S = 16;
    const float SPACE_M = 24;
    const float SPACE_L = 32;
    const float SPACE_XL = 48;
    const float SPACE_XXL = 64;

    // radii
    const float RADIUS_LARGE = 24;  // hero, large panels
    const float RADIUS = 16;        // cards, panels
    const float RADIUS_SMALL = 10;  // rows, posters, keys

    // focus
    const float FOCUS_BORDER = 4;   // cards, posters, buttons
    const float ROW_FOCUS_BORDER = 3;   // list rows
    const float FOCUS_LIFT = 6;     // a focused poster rises this much (no animation, no shadow)

    // common sizes
    const float BUTTON_H = 76;
    const float CHIP_H = 56;
    const float PROGRESS_H = 6;
    const float ICON_BUTTON = 64;   // round icon buttons in the top navigation
    const float POSTER_W = 140;     // grid poster = the Poster image box (ImageLoader), never upscaled
    const float POSTER_H = 210;

    // ---------------------------------------------------------------- type scale (px)
    // Display: hero titles. Title: screen and detail titles. Heading: section / rail headings and panel titles.
    // Body: primary text and buttons. Label: metadata, row secondary text. Caption: hints, badges, fine print.
    const unsigned DISPLAY = 64;
    const unsigned TITLE = 44;
    const unsigned HEADING = 34;
    const unsigned BODY = 28;
    const unsigned LABEL = 24;
    const unsigned CAPTION = 22;

    // ---------------------------------------------------------------- motion (seconds)
    const double MOTION_FAST = 0.12;    // focus / opacity steps
    const double MOTION_PANEL = 0.20;   // panel and rail glides (only while Smooth scrolling is on)

    inline c2d::Color color(palette::Rgba v) { return {v.r, v.g, v.b, v.a}; }

    // ---------------------------------------------------------------- accent (Settings > Appearance)
    inline int &accentSlot() {
        static int index = palette::DEFAULT_ACCENT;
        return index;
    }

    // out-of-range values fall back to the default (V7 Blue)
    inline void setAccent(int index) {
        accentSlot() = index >= 0 && index < palette::ACCENT_COUNT ? index : palette::DEFAULT_ACCENT;
    }

    inline int accentIndex() { return accentSlot(); }

    inline const palette::Accent &accentPreset() { return palette::ACCENTS[accentSlot()]; }

    inline c2d::Color accent() { return color(accentPreset().accent); }

    inline c2d::Color accentText() { return color(accentPreset().text); }

    inline c2d::Color accentMuted() { return color(accentPreset().muted); }

    // focused list row: dark, slightly accent-tinted
    inline c2d::Color focus() { return color(accentPreset().focus); }

    // captions on an accent fill (primary button, selected chip when focused)
    inline c2d::Color onAccent() { return color(accentPreset().onAccent); }

    // a preset's swatch (Appearance settings), whatever the active accent is
    inline c2d::Color swatch(int index) { return color(palette::ACCENTS[index].accent); }

    // ---------------------------------------------------------------- surfaces
    inline c2d::Color background() { return color(palette::BACKGROUND); }
    inline c2d::Color backgroundSecondary() { return color(palette::BACKGROUND_SECONDARY); }
    inline c2d::Color surface() { return color(palette::SURFACE); }
    inline c2d::Color card() { return color(palette::CARD); }
    inline c2d::Color surfaceElevated() { return color(palette::SURFACE_ELEVATED); }
    inline c2d::Color cardHover() { return color(palette::CARD_HOVER); }
    inline c2d::Color panel() { return color(palette::PANEL); }
    inline c2d::Color divider() { return color(palette::DIVIDER); }
    // tile behind channel logos: lighter than the rows, so dark and transparent logos stay readable
    inline c2d::Color logoTile() { return color(palette::LOGO_TILE); }

    // ---------------------------------------------------------------- text
    inline c2d::Color textStrong() { return color(palette::TEXT_STRONG); }
    inline c2d::Color textPrimary() { return color(palette::TEXT_PRIMARY); }
    inline c2d::Color textSecondary() { return color(palette::TEXT_SECONDARY); }
    inline c2d::Color textMuted() { return color(palette::TEXT_MUTED); }

    // ---------------------------------------------------------------- states (never accent-coloured)
    inline c2d::Color success() { return color(palette::SUCCESS); }
    inline c2d::Color warning() { return color(palette::WARNING); }
    inline c2d::Color danger() { return color(palette::DANGER); }
    inline c2d::Color successSurface() { return color(palette::SUCCESS_SURFACE); }
    inline c2d::Color dangerSurface() { return color(palette::DANGER_SURFACE); }

    // ---------------------------------------------------------------- overlays
    inline c2d::Color overlay() { return color(palette::OVERLAY); }
    inline c2d::Color overlayPanel() { return color(palette::OVERLAY_PANEL); }
    inline c2d::Color scrim() { return color(palette::SCRIM); }
    inline c2d::Color videoBackground() { return color(palette::VIDEO_BACKGROUND); }
    inline c2d::Color progressTrack() { return color(palette::PROGRESS_TRACK); }
    inline c2d::Color progressTrackOnImage() { return color(palette::PROGRESS_TRACK_ON_IMAGE); }
    inline c2d::Color backdropTint() { return color(palette::BACKDROP_TINT); }
    inline c2d::Color backdropShade() { return color(palette::BACKDROP_SHADE); }

    // ---------------------------------------------------------------- controls
    inline c2d::Color scrollTrack() { return color(palette::SCROLL_TRACK); }
    inline c2d::Color scrollThumb() { return color(palette::SCROLL_THUMB); }
    inline c2d::Color scrollThumbIdle() { return color(palette::SCROLL_THUMB_IDLE); }
    inline c2d::Color switchOff() { return color(palette::SWITCH_OFF); }
    inline c2d::Color switchKnobOff() { return color(palette::SWITCH_KNOB_OFF); }
    inline c2d::Color padCross() { return color(palette::PAD_CROSS); }
    inline c2d::Color padCircle() { return color(palette::PAD_CIRCLE); }
    inline c2d::Color padSquare() { return color(palette::PAD_SQUARE); }
    inline c2d::Color padTriangle() { return color(palette::PAD_TRIANGLE); }
    inline c2d::Color monogram(unsigned i) { return color(palette::MONOGRAM[i % 8]); }

    // texture modulation colour: images drawn as they are (not a UI colour)
    inline c2d::Color untinted() { return {255, 255, 255, 255}; }

    // fully transparent (layers, unfocused rows)
    inline c2d::Color none() { return {0, 0, 0, 0}; }

    inline c2d::Color withAlpha(c2d::Color c, uint8_t a) {
        c.a = a;
        return c;
    }
}

#endif // PS4IPTV_UI_THEME_H
