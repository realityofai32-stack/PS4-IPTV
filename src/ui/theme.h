// Visual design tokens for the 1920x1080 TV layout.

#ifndef PS4IPTV_UI_THEME_H
#define PS4IPTV_UI_THEME_H

#include "cross2d/c2d.h"

namespace theme {

    // layout
    const float SCREEN_W = 1920;
    const float SCREEN_H = 1080;
    const float SAFE_X = 96;        // ~5% TV-safe area
    const float SAFE_Y = 54;
    const float TOPBAR_H = 108;
    const float RADIUS = 16;
    const float RADIUS_SMALL = 10;
    const float FOCUS_BORDER = 4;

    // type scale (px)
    const unsigned DISPLAY = 64;
    const unsigned TITLE = 44;
    const unsigned HEADING = 34;
    const unsigned BODY = 28;
    const unsigned LABEL = 24;
    const unsigned CAPTION = 22;

    // palette: dark neutral blue-grey with one accent
    inline c2d::Color bgTop() { return {13, 17, 24}; }
    inline c2d::Color bgBottom() { return {20, 26, 36}; }
    inline c2d::Color surface() { return {28, 35, 47}; }
    inline c2d::Color surfaceRaised() { return {38, 47, 62}; }
    inline c2d::Color surfaceFocus() { return {52, 64, 84}; }
    inline c2d::Color divider() { return {48, 58, 74}; }
    inline c2d::Color text() { return {236, 240, 245}; }
    inline c2d::Color textDim() { return {160, 171, 186}; }
    inline c2d::Color textMuted() { return {104, 116, 133}; }
    inline c2d::Color accent() { return {72, 149, 255}; }
    inline c2d::Color accentDark() { return {38, 98, 196}; }
    inline c2d::Color success() { return {52, 199, 140}; }
    inline c2d::Color warning() { return {255, 184, 48}; }
    inline c2d::Color danger() { return {255, 92, 99}; }
    inline c2d::Color scrim() { return {6, 8, 12, 200}; }
    // focused list row: accent-tinted surface (with an accent outline)
    inline c2d::Color rowFocus() { return {34, 62, 104}; }
    // tile behind channel logos: lighter than the rows, so dark and transparent logos stay readable
    inline c2d::Color logoTile() { return {54, 64, 82}; }
    // scrollbar: thin translucent track, rounded thumb
    inline c2d::Color scrollTrack() { return {255, 255, 255, 20}; }
    inline c2d::Color scrollThumb() { return {255, 255, 255, 150}; }
    inline c2d::Color scrollThumbIdle() { return {255, 255, 255, 80}; }

    inline c2d::Color withAlpha(c2d::Color c, uint8_t a) {
        c.a = a;
        return c;
    }
}

#endif // PS4IPTV_UI_THEME_H
