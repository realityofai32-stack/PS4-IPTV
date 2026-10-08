// Visual design tokens for the 1920x1080 TV layout: one polished near-black theme.
//
// Every screen colour comes from here (tests/host/test_theme.cpp rejects colour literals in screens and
// widgets). The values live in ui/palette.h, where the host tests check text contrast on every surface.
//
// Hierarchy: background (near-black gradient) < surface (panels, rows) < card (tiles) < surfaceElevated
// (buttons, chips) < cardHover (focused card). Focus is a dark highlighted row / card with a restrained accent
// border; the accent fills only small things (focused buttons, markers, progress).

#ifndef PS4IPTV_UI_THEME_H
#define PS4IPTV_UI_THEME_H

#include "cross2d/c2d.h"
#include "palette.h"

namespace theme {

    // layout
    const float SCREEN_W = 1920;
    const float SCREEN_H = 1080;
    const float SAFE_X = 96;        // ~5% TV-safe area
    const float SAFE_Y = 54;
    const float TOPBAR_H = 108;
    const float RADIUS = 16;
    const float RADIUS_SMALL = 10;
    const float FOCUS_BORDER = 4;   // cards and posters
    const float ROW_FOCUS_BORDER = 3;   // list rows
    const float FOCUS_LIFT = 6;     // a focused poster rises this much (no animation, no shadow)

    // type scale (px)
    const unsigned DISPLAY = 64;
    const unsigned TITLE = 44;
    const unsigned HEADING = 34;
    const unsigned BODY = 28;
    const unsigned LABEL = 24;
    const unsigned CAPTION = 22;

    inline c2d::Color color(palette::Rgba v) { return {v.r, v.g, v.b, v.a}; }

    // surfaces
    inline c2d::Color background() { return color(palette::BACKGROUND); }
    inline c2d::Color backgroundSecondary() { return color(palette::BACKGROUND_SECONDARY); }
    inline c2d::Color surface() { return color(palette::SURFACE); }
    inline c2d::Color card() { return color(palette::CARD); }
    inline c2d::Color surfaceElevated() { return color(palette::SURFACE_ELEVATED); }
    inline c2d::Color cardHover() { return color(palette::CARD_HOVER); }
    inline c2d::Color focus() { return color(palette::FOCUS); }
    inline c2d::Color panel() { return color(palette::PANEL); }
    inline c2d::Color divider() { return color(palette::DIVIDER); }
    // tile behind channel logos: lighter than the rows, so dark and transparent logos stay readable
    inline c2d::Color logoTile() { return color(palette::LOGO_TILE); }

    // text
    inline c2d::Color textStrong() { return color(palette::TEXT_STRONG); }
    inline c2d::Color textPrimary() { return color(palette::TEXT_PRIMARY); }
    inline c2d::Color textSecondary() { return color(palette::TEXT_SECONDARY); }
    inline c2d::Color textMuted() { return color(palette::TEXT_MUTED); }

    // accent and states
    inline c2d::Color accent() { return color(palette::ACCENT); }
    inline c2d::Color accentText() { return color(palette::ACCENT_TEXT); }
    inline c2d::Color accentMuted() { return color(palette::ACCENT_MUTED); }
    inline c2d::Color success() { return color(palette::SUCCESS); }
    inline c2d::Color warning() { return color(palette::WARNING); }
    inline c2d::Color danger() { return color(palette::DANGER); }
    inline c2d::Color successSurface() { return color(palette::SUCCESS_SURFACE); }
    inline c2d::Color dangerSurface() { return color(palette::DANGER_SURFACE); }

    // overlays
    inline c2d::Color overlay() { return color(palette::OVERLAY); }
    inline c2d::Color overlayPanel() { return color(palette::OVERLAY_PANEL); }
    inline c2d::Color scrim() { return color(palette::SCRIM); }
    inline c2d::Color videoBackground() { return color(palette::VIDEO_BACKGROUND); }
    inline c2d::Color progressTrack() { return color(palette::PROGRESS_TRACK); }
    inline c2d::Color progressTrackOnImage() { return color(palette::PROGRESS_TRACK_ON_IMAGE); }

    // controls
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
