// The V7 Player brand artwork as coloured triangles (host-testable: no renderer, no textures).
//
// One description, three uses:
//   - the in-app loading screen (ui/splash.h): drawn with the accent the user chose (Settings > Appearance),
//     animated by time;
//   - the PS4 package images, sce_sys/pic1.png (the system's start image: the loading screen's first frame in
//     V7 Blue) and sce_sys/icon0.png, rasterized on the host by assets/make_brand.sh from these same triangles,
//     so the system image and the app's first frame line up;
//   - the V7 monogram of ui::BrandMark (headers, navigation, onboarding): the same pieces as flat shapes.
//
// The monogram: a V and a 7 on a 100 x 54 grid. Every slanted edge runs 0.6 units across per unit down; the V's
// right arm is the 7's parallel twin, and a short curved hook under the 7's bar points into the V.
// Colours come from the accent presets (ui/palette.h): the accent itself, its `glow` (the brightest tone) and a
// darker tone derived from the accent - one implementation for every accent, no per-theme artwork.

#ifndef PS4IPTV_UI_BRAND_ART_H
#define PS4IPTV_UI_BRAND_ART_H

#include <vector>

#include "palette.h"

namespace brandart {

    struct Point {
        float x, y;
    };

    struct Vertex {
        float x, y;
        palette::Rgba c;
    };

    // triangles, 3 vertices each, drawn in order with "source over" blending
    using Mesh = std::vector<Vertex>;

    constexpr float GRID_W = 100.0f;
    constexpr float GRID_H = 54.0f;
    constexpr float ASPECT = GRID_W / GRID_H;   // width / height

    // the monogram's pieces on the grid (origin top-left, y down); each piece is triangulated as a fan from its
    // first point (every piece is convex, or star-shaped around its first point: the hook)
    const std::vector<std::vector<Point>> &monogram();

    struct Colors {
        palette::Rgba hi, mid, deep;          // monogram: top -> 55 % -> bottom
        palette::Rgba halo, glow, core;       // light ribbons: wide halo, glow, bright core
        palette::Rgba dot, dotActive;         // loading dots
        palette::Rgba background;             // the page
    };

    // every colour from one accent preset
    Colors colorsOf(const palette::Accent &accent);

    // the monogram with its vertical gradient, top-left corner at (x, y), `height` px tall. feather: width of the
    // soft (anti-aliased) edge in px, 0 = hard edges.
    void addMonogram(Mesh &out, float x, float y, float height, const Colors &c, float feather = 1.25f);

    // a soft elliptical glow: `color` at `alpha` in the centre, fading linearly to nothing at the radii
    void addGlow(Mesh &out, float cx, float cy, float rx, float ry, palette::Rgba color, float alpha, int segments = 48);

    // an axis-aligned rectangle with a vertical gradient
    void addRect(Mesh &out, float x, float y, float w, float h, palette::Rgba top, palette::Rgba bottom);

    // ---------------------------------------------------------------- the loading screen (1920 x 1080)
    namespace splash {
        constexpr float W = 1920.0f;
        constexpr float H = 1080.0f;
        constexpr float LOGO_H = 290.0f;                    // monogram height (537 px wide)
        constexpr float LOGO_CY = 470.0f;                   // monogram centre
        constexpr float DOTS_Y = 918.0f;
        constexpr int DOTS = 5;
        constexpr float DOT_R = 14.0f;
        constexpr float DOT_GAP = 58.0f;                    // centre to centre
        constexpr int WAVES = 3;
        constexpr int WAVE_SEGMENTS = 96;
        // the animation: every motion is a slow continuous function of time (no frame counting): the wave
        // drift, a faint shimmer, the highlight gliding over the dots
        constexpr double WAVE_PERIOD = 9.0;                 // s, one drift cycle
        constexpr float WAVE_AMPLITUDE = 10.0f;             // px, largest vertical drift
        constexpr double DOTS_PERIOD = 2.8;                 // s, the highlight's trip there and back

        // the page and the glows behind the monogram: static
        void background(Mesh &out, const Colors &c);
        // the monogram: static
        void logo(Mesh &out, const Colors &c);
        // the light ribbons at time t (s since the screen appeared)
        void waves(Mesh &out, const Colors &c, double t);
        // the loading dots at time t
        void dots(Mesh &out, const Colors &c, double t);
        // everything, in drawing order; frame(V7 Blue, 0) is the package start image
        void frame(Mesh &out, const Colors &c, double t);

        // a wave's centre line at x (px) and time t: for tests (the drift stays within WAVE_AMPLITUDE)
        float waveY(int wave, float x, double t);
        // the dots' brightness 0..1 at time t
        float dotLevel(int dot, double t);
    }

    // ---------------------------------------------------------------- the package icon (512 x 512)
    namespace icon {
        constexpr int SIZE = 512;
        constexpr float LOGO_H = 200.0f;                    // monogram height (370 px wide)
        void frame(Mesh &out, const Colors &c);
    }
}

#endif // PS4IPTV_UI_BRAND_ART_H
