#include <algorithm>
#include <cmath>

#include "brand_art.h"

namespace brandart {

    using palette::Rgba;

    namespace {
        const float PI = 3.14159265358979f;
        const float MID = 0.55f;   // the monogram's middle gradient stop (relative height)

        float clamp01(float v) {
            return v < 0 ? 0 : v > 1 ? 1 : v;
        }

        uint8_t channel(float v) {
            return (uint8_t) std::lround(std::max(0.0f, std::min(255.0f, v)));
        }

        Rgba mix(Rgba a, Rgba b, float t) {
            t = clamp01(t);
            return {channel(a.r + (b.r - a.r) * t), channel(a.g + (b.g - a.g) * t), channel(a.b + (b.b - a.b) * t),
                    channel(a.a + (b.a - a.a) * t)};
        }

        Rgba withAlpha(Rgba c, float alpha) {
            c.a = channel(clamp01(alpha) * 255.0f);
            return c;
        }

        // a darker, slightly richer tone of a colour: HSV saturation x 1.1, value x 0.8
        Rgba deepOf(Rgba c) {
            float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
            float mx = std::max(r, std::max(g, b));
            float mn = std::min(r, std::min(g, b));
            if (mx <= 0) {
                return c;
            }
            float s = (mx - mn) / mx;
            float v2 = mx * 0.8f;
            float s2 = std::min(1.0f, s * 1.1f);
            // the same hue: every channel keeps its relative position between min and max
            auto tone = [&](float x) {
                float rel = mx > mn ? (x - mn) / (mx - mn) : 1.0f;
                float lo = v2 * (1 - s2);
                return channel((lo + (v2 - lo) * rel) * 255.0f);
            };
            return {tone(r), tone(g), tone(b), 255};
        }

        Rgba gradient(const Colors &c, float v) {
            v = clamp01(v);
            return v <= MID ? mix(c.hi, c.mid, v / MID) : mix(c.mid, c.deep, (v - MID) / (1 - MID));
        }

        void tri(Mesh &out, const Vertex &a, const Vertex &b, const Vertex &c) {
            out.push_back(a);
            out.push_back(b);
            out.push_back(c);
        }

        // the part of a convex polygon above (y <= limit) or below (y >= limit) a horizontal line
        std::vector<Point> clipY(const std::vector<Point> &poly, float limit, bool above) {
            std::vector<Point> out;
            const size_t n = poly.size();
            for (size_t i = 0; i < n; i++) {
                const Point &a = poly[i];
                const Point &b = poly[(i + 1) % n];
                bool ina = above ? a.y <= limit : a.y >= limit;
                bool inb = above ? b.y <= limit : b.y >= limit;
                if (ina) {
                    out.push_back(a);
                }
                if (ina != inb) {
                    float t = (limit - a.y) / (b.y - a.y);
                    out.push_back({a.x + (b.x - a.x) * t, limit});
                }
            }
            return out;
        }

        float signedArea(const std::vector<Point> &p) {
            float a = 0;
            for (size_t i = 0; i < p.size(); i++) {
                const Point &u = p[i];
                const Point &v = p[(i + 1) % p.size()];
                a += u.x * v.y - v.x * u.y;
            }
            return a / 2;
        }

        std::vector<Point> bezier(Point p0, Point c, Point p2, int n) {
            std::vector<Point> pts;
            for (int i = 0; i <= n; i++) {
                float t = (float) i / (float) n;
                float u = 1 - t;
                pts.push_back({u * u * p0.x + 2 * t * u * c.x + t * t * p2.x, u * u * p0.y + 2 * t * u * c.y + t * t * p2.y});
            }
            return pts;
        }

        // ---------------------------------------------------------------- light ribbons
        struct Key {
            float x, y, level;   // x, y relative to the screen; level: brightness 0..1 at this point
        };

        struct Wave {
            std::vector<Key> keys;
            float weight;        // brightness and width of the whole ribbon
            float wavelength;    // px, of the drift
            float phase;
            float amplitude;     // px
        };

        // from the reference composition: the main ribbon rises from the lower left, runs low and faint under the
        // monogram and sweeps up to the right edge; a second one runs above it, a faint third one below
        const std::vector<Wave> &waveSet() {
            static const std::vector<Wave> w = {
                    {{{-0.03f, 0.975f, 0.70f}, {0.08f, 0.875f, 1.0f}, {0.20f, 0.80f, 0.80f}, {0.34f, 0.775f, 0.30f},
                      {0.50f, 0.79f, 0.10f}, {0.64f, 0.78f, 0.30f}, {0.76f, 0.71f, 0.80f}, {0.88f, 0.59f, 1.0f},
                      {1.03f, 0.43f, 0.85f}}, 1.0f, 1500.0f, 0.0f, 10.0f},
                    {{{-0.03f, 0.865f, 0.40f}, {0.10f, 0.735f, 0.62f}, {0.22f, 0.67f, 0.40f}, {0.34f, 0.66f, 0.10f},
                      {0.48f, 0.70f, 0.03f}, {0.62f, 0.745f, 0.10f}, {0.75f, 0.665f, 0.50f}, {0.87f, 0.52f, 0.75f},
                      {1.03f, 0.29f, 0.60f}}, 0.75f, 1250.0f, 2.1f, 8.0f},
                    {{{-0.03f, 1.035f, 0.25f}, {0.12f, 0.975f, 0.30f}, {0.28f, 0.935f, 0.22f}, {0.42f, 0.905f, 0.12f},
                      {0.56f, 0.895f, 0.10f}, {0.70f, 0.855f, 0.26f}, {0.80f, 0.80f, 0.42f}, {0.90f, 0.735f, 0.50f},
                      {1.03f, 0.63f, 0.40f}}, 0.55f, 1750.0f, 4.2f, 7.0f}};
            return w;
        }

        float catmull(float p0, float p1, float p2, float p3, float t) {
            float t2 = t * t, t3 = t2 * t;
            return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
        }

        struct Sample {
            float x, y, level;
        };

        // the ribbon's centre line: WAVE_SEGMENTS + 1 points, drifted for time t
        std::vector<Sample> sampleWave(int index, double t) {
            const Wave &w = waveSet()[(size_t) index];
            const auto &k = w.keys;
            const int segs = (int) k.size() - 1;
            const int per = std::max(1, splash::WAVE_SEGMENTS / segs);
            std::vector<Sample> s;
            const float phaseT = (float) (2.0 * PI * std::fmod(t, splash::WAVE_PERIOD) / splash::WAVE_PERIOD);
            for (int i = 0; i < segs; i++) {
                const Key &k0 = k[(size_t) std::max(0, i - 1)];
                const Key &k1 = k[(size_t) i];
                const Key &k2 = k[(size_t) i + 1];
                const Key &k3 = k[(size_t) std::min(segs, i + 2)];
                for (int j = 0; j < per || (i == segs - 1 && j == per); j++) {
                    float u = (float) j / (float) per;
                    float x = catmull(k0.x, k1.x, k2.x, k3.x, u) * splash::W;
                    float y = catmull(k0.y, k1.y, k2.y, k3.y, u) * splash::H;
                    float level = clamp01(catmull(k0.level, k1.level, k2.level, k3.level, u));
                    // the drift: a long, slow travelling swell; the shimmer: a slight brightness ripple along it
                    y += w.amplitude * std::sin(2 * PI * x / w.wavelength + w.phase - phaseT);
                    level *= 1.0f + 0.10f * std::sin(2 * PI * x / 900.0f + w.phase - 1.4f * phaseT);
                    s.push_back({x, y, clamp01(level)});
                }
            }
            return s;
        }

        // one layer of a ribbon: `color` at `alpha` x level from the centre line out to `plateau` x halfWidth, then
        // fading linearly to nothing at `halfWidth` on both sides
        void ribbon(Mesh &out, const std::vector<Sample> &s, float halfWidth, Rgba color, float alpha,
                    float plateau = 0.0f) {
            const size_t n = s.size();
            if (n < 2) {
                return;
            }
            std::vector<Point> normals(n);
            for (size_t i = 0; i < n; i++) {
                const Sample &a = s[i == 0 ? 0 : i - 1];
                const Sample &b = s[i + 1 < n ? i + 1 : n - 1];
                float dx = b.x - a.x, dy = b.y - a.y;
                float len = std::sqrt(dx * dx + dy * dy);
                normals[i] = len > 0 ? Point{-dy / len, dx / len} : Point{0, 1};
            }
            // across the ribbon: edge, plateau edge, centre, plateau edge, edge (the plateau may be empty)
            const float offsets[5] = {1.0f, plateau, 0.0f, -plateau, -1.0f};
            auto at = [&](size_t i, int k) {
                float a = k == 0 || k == 4 ? 0.0f : alpha * s[i].level;
                float o = offsets[k] * halfWidth;
                return Vertex{s[i].x + normals[i].x * o, s[i].y + normals[i].y * o, withAlpha(color, a)};
            };
            for (size_t i = 0; i + 1 < n; i++) {
                for (int k = 0; k < 4; k++) {
                    if (plateau <= 0 && (k == 1 || k == 2)) {
                        continue;   // no plateau: the two sides meet at the centre line
                    }
                    Vertex a0 = at(i, k), b0 = at(i, k + 1), a1 = at(i + 1, k), b1 = at(i + 1, k + 1);
                    tri(out, a0, b0, a1);
                    tri(out, a1, b0, b1);
                }
            }
        }

        // a filled disc with a soft (anti-aliased) rim
        void disc(Mesh &out, float cx, float cy, float r, Rgba color, float feather = 1.2f) {
            const int n = 32;
            Vertex centre{cx, cy, color};
            for (int i = 0; i < n; i++) {
                float a0 = 2 * PI * (float) i / n, a1 = 2 * PI * (float) (i + 1) / n;
                Vertex p0{cx + r * std::cos(a0), cy + r * std::sin(a0), color};
                Vertex p1{cx + r * std::cos(a1), cy + r * std::sin(a1), color};
                Vertex q0{cx + (r + feather) * std::cos(a0), cy + (r + feather) * std::sin(a0), withAlpha(color, 0)};
                Vertex q1{cx + (r + feather) * std::cos(a1), cy + (r + feather) * std::sin(a1), withAlpha(color, 0)};
                tri(out, centre, p0, p1);
                tri(out, p0, q0, p1);
                tri(out, p1, q0, q1);
            }
        }
    }

    const std::vector<std::vector<Point>> &monogram() {
        static const std::vector<std::vector<Point>> pieces = [] {
            std::vector<std::vector<Point>> p;
            // V: the left arm down to the inner corner, the point between the two outer edges, the right arm
            p.push_back({{0, 0}, {17.5f, 0}, {38.92f, 35.7f}, {21.42f, 35.7f}});
            p.push_back({{21.42f, 35.7f}, {53.78f, 35.7f}, {42.8f, 54}, {32.4f, 54}});
            p.push_back({{47.5f, 21.4f}, {62.36f, 21.4f}, {53.78f, 35.7f}, {38.92f, 35.7f}});
            // 7: the bar, the stem
            p.push_back({{48.9f, 0}, {100, 0}, {91.66f, 13.9f}, {40.56f, 13.9f}});
            p.push_back({{76.06f, 13.9f}, {91.66f, 13.9f}, {67.6f, 54}, {52.0f, 54}});
            // the hook: from the bar's lower left corner, a concave curve down to a point on the bar's left edge
            std::vector<Point> hook = {{40.56f, 13.9f}};
            for (const Point &q: bezier({49.5f, 13.9f}, {41.5f, 14.6f}, {36.78f, 20.2f}, 10)) {
                hook.push_back(q);
            }
            p.push_back(hook);
            return p;
        }();
        return pieces;
    }

    Colors colorsOf(const palette::Accent &a) {
        Colors c;
        c.hi = a.glow;
        c.mid = a.accent;
        c.deep = deepOf(a.accent);
        c.halo = a.accent;
        c.glow = mix(a.accent, a.glow, 0.45f);
        c.core = mix(a.glow, palette::rgb(255, 255, 255), 0.3f);
        c.dot = mix(a.glow, a.accent, 0.25f);
        c.dotActive = mix(a.glow, palette::rgb(255, 255, 255), 0.5f);
        c.background = palette::BACKGROUND;
        return c;
    }

    void addMonogram(Mesh &out, float x, float y, float height, const Colors &c, float feather) {
        const float s = height / GRID_H;
        auto vert = [&](const Point &p, float alpha) {
            return Vertex{x + p.x * s, y + p.y * s, withAlpha(gradient(c, p.y / GRID_H), alpha)};
        };
        const float split = MID * GRID_H;   // the gradient stop: vertices there keep the gradient exact
        for (const auto &piece: monogram()) {
            float top = piece[0].y, bottom = piece[0].y;
            for (const Point &p: piece) {
                top = std::min(top, p.y);
                bottom = std::max(bottom, p.y);
            }
            std::vector<std::vector<Point>> parts;
            if (top < split && bottom > split) {
                parts.push_back(clipY(piece, split, true));   // convex pieces only cross the stop
                parts.push_back(clipY(piece, split, false));
            } else {
                parts.push_back(piece);
            }
            for (const auto &part: parts) {
                for (size_t i = 1; i + 1 < part.size(); i++) {
                    tri(out, vert(part[0], 1), vert(part[i], 1), vert(part[i + 1], 1));
                }
            }
        }
        if (feather <= 0) {
            return;
        }
        // soft edges: a thin band outside every edge, from the fill colour to transparent (edges inside the symbol
        // fall on fill of the same colour: invisible)
        const float f = feather / s;
        for (const auto &piece: monogram()) {
            const float sign = signedArea(piece) > 0 ? 1.0f : -1.0f;
            for (size_t i = 0; i < piece.size(); i++) {
                const Point &a = piece[i];
                const Point &b = piece[(i + 1) % piece.size()];
                float dx = b.x - a.x, dy = b.y - a.y;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len <= 0) {
                    continue;
                }
                Point n{sign * dy / len * f, -sign * dx / len * f};
                Point ao{a.x + n.x, a.y + n.y}, bo{b.x + n.x, b.y + n.y};
                tri(out, vert(a, 1), vert(b, 1), vert(ao, 0));
                tri(out, vert(b, 1), vert(bo, 0), vert(ao, 0));
            }
        }
    }

    void addGlow(Mesh &out, float cx, float cy, float rx, float ry, Rgba color, float alpha, int segments) {
        Vertex centre{cx, cy, withAlpha(color, alpha)};
        for (int i = 0; i < segments; i++) {
            float a0 = 2 * PI * (float) i / (float) segments, a1 = 2 * PI * (float) (i + 1) / (float) segments;
            tri(out, centre, {cx + rx * std::cos(a0), cy + ry * std::sin(a0), withAlpha(color, 0)},
                {cx + rx * std::cos(a1), cy + ry * std::sin(a1), withAlpha(color, 0)});
        }
    }

    void addRect(Mesh &out, float x, float y, float w, float h, Rgba top, Rgba bottom) {
        Vertex a{x, y, top}, b{x + w, y, top}, c{x + w, y + h, bottom}, d{x, y + h, bottom};
        tri(out, a, b, c);
        tri(out, a, c, d);
    }

    namespace splash {

        void background(Mesh &out, const Colors &c) {
            addRect(out, 0, 0, W, H, c.background, c.background);
            // the light's ambience: low along the bottom, a faint lift where the ribbons rise on the right,
            // a soft bloom behind the monogram
            addGlow(out, W / 2, H + 120, 1250, 520, c.halo, 0.10f);
            addGlow(out, W * 0.95f, H * 0.50f, 640, 470, c.halo, 0.10f);
            addGlow(out, W * 0.03f, H * 0.93f, 620, 380, c.halo, 0.10f);
            addGlow(out, W / 2, LOGO_CY, 640, 350, c.halo, 0.11f);
            addGlow(out, W / 2, LOGO_CY, 380, 210, c.glow, 0.06f);
        }

        void logo(Mesh &out, const Colors &c) {
            const float w = LOGO_H * ASPECT;
            addMonogram(out, std::round(W / 2 - w / 2), std::round(LOGO_CY - LOGO_H / 2), LOGO_H, c);
        }

        void waves(Mesh &out, const Colors &c, double t) {
            for (int i = 0; i < WAVES; i++) {
                const float weight = waveSet()[(size_t) i].weight;
                std::vector<Sample> s = sampleWave(i, t);
                ribbon(out, s, 160 * weight, c.halo, 0.20f * weight);         // the wide halo
                ribbon(out, s, 54 * weight, c.halo, 0.62f * weight, 0.3f);    // the ribbon's body
                ribbon(out, s, 16 * weight, c.glow, 0.78f * weight, 0.2f);
                ribbon(out, s, 3.6f, c.core, 0.95f * weight);                 // the bright core
            }
        }

        float waveY(int wave, float x, double t) {
            std::vector<Sample> s = sampleWave(wave, t);
            for (size_t i = 0; i + 1 < s.size(); i++) {
                if (x >= s[i].x && x <= s[i + 1].x) {
                    float u = s[i + 1].x > s[i].x ? (x - s[i].x) / (s[i + 1].x - s[i].x) : 0;
                    return s[i].y + (s[i + 1].y - s[i].y) * u;
                }
            }
            return s.empty() ? 0 : s.back().y;
        }

        float dotLevel(int dot, double t) {
            // the highlight glides from the middle dot to the ends and back
            double p = 2.0 + 2.0 * std::sin(2.0 * PI * std::fmod(t, DOTS_PERIOD) / DOTS_PERIOD);
            double d = (double) dot - p;
            return (float) std::exp(-d * d / 0.55);
        }

        void dots(Mesh &out, const Colors &c, double t) {
            for (int i = 0; i < DOTS; i++) {
                float level = dotLevel(i, t);
                float x = W / 2 + (float) (i - DOTS / 2) * DOT_GAP;
                Rgba base = mix(c.dot, c.deep, 0.75f * (float) i / (DOTS - 1));
                addGlow(out, x, DOTS_Y, DOT_R * 3.0f, DOT_R * 3.0f, c.halo, 0.16f + 0.24f * level, 24);
                disc(out, x, DOTS_Y, DOT_R * (1.0f + 0.10f * level),
                     withAlpha(mix(base, c.dotActive, level), 1.0f));
            }
        }

        void frame(Mesh &out, const Colors &c, double t) {
            background(out, c);
            waves(out, c, t);
            logo(out, c);
            dots(out, c, t);
        }
    }

    namespace icon {

        void frame(Mesh &out, const Colors &c) {
            const float s = (float) SIZE;
            addRect(out, 0, 0, s, s, mix(c.background, c.mid, 0.05f), c.background);
            addGlow(out, s / 2, s * 0.53f, s * 0.62f, s * 0.52f, c.halo, 0.24f);
            addGlow(out, s / 2, s * 0.51f, s * 0.36f, s * 0.24f, c.glow, 0.10f);
            const float w = LOGO_H * ASPECT;
            addMonogram(out, std::round(s / 2 - w / 2), std::round(s / 2 - LOGO_H / 2 + 4), LOGO_H, c, 1.0f);
        }
    }
}
