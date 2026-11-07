// Vector icon geometry (host-testable, no libcross2d): the navigation's Settings gear (v7-ux-10).
//
// One gear silhouette drawn as an outline - the same stroke language as Search (ring) and Favorites (star): eight
// trapezoid teeth around a body, every point star-shaped around the centre (the shape's fan triangulation and its
// outline both work, like the star), plus a small outlined hub. Sizes are fractions of the button size, matching the
// other icons (star radius 0.24).

#ifndef PS4IPTV_UI_ICON_SHAPES_H
#define PS4IPTV_UI_ICON_SHAPES_H

#include <cmath>
#include <vector>

namespace ui {
    namespace icons {

        constexpr int GEAR_TEETH = 8;
        constexpr float GEAR_OUTER = 0.235f;    // tooth tips (x button size)
        constexpr float GEAR_INNER = 0.175f;    // the body between the teeth
        constexpr float GEAR_HUB = 0.07f;       // the hole
        constexpr float GEAR_TIP_HALF = 0.20f;  // half a tooth's angular width at its tip, x the tooth pitch
        constexpr float GEAR_BASE_HALF = 0.30f; // ... at its base (wider: a trapezoid tooth)

        struct Point {
            float x, y;
        };

        // the gear's outline, clockwise from the first tooth, around (cx, cy) for a button of size s: 4 points a tooth
        inline std::vector<Point> gear(float cx, float cy, float s) {
            const float pi = 3.14159265f;
            const float pitch = 2 * pi / (float) GEAR_TEETH;
            std::vector<Point> p;
            for (int i = 0; i < GEAR_TEETH; i++) {
                const float a = -pi / 2 + (float) i * pitch;
                const float pts[4][2] = {{GEAR_INNER, a - GEAR_BASE_HALF * pitch}, {GEAR_OUTER, a - GEAR_TIP_HALF * pitch},
                                         {GEAR_OUTER, a + GEAR_TIP_HALF * pitch}, {GEAR_INNER, a + GEAR_BASE_HALF * pitch}};
                for (const auto &q: pts) {
                    p.push_back({cx + q[0] * s * std::cos(q[1]), cy + q[0] * s * std::sin(q[1])});
                }
            }
            return p;
        }
    }
}

#endif // PS4IPTV_UI_ICON_SHAPES_H
