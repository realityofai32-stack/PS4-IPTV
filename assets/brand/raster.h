// Software rasterizer for brandart meshes (src/ui/brand_art.h) - host only, never part of the PS4 build: renders
// the package images (assets/make_brand.sh) and lets tests/host/test_brand.cpp check that the committed images
// still match the geometry the app draws.
//
// Triangles are drawn in order with "source over" blending of their linearly interpolated vertex colours, like the
// app's OpenGL ES path (libcross2d, glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)), on a supersampled grid
// (`ss` x `ss` samples per pixel, box-filtered). The result is RGB, 8 bits per channel, with an optional ordered
// dither (no banding in the dark gradients).

#ifndef PS4IPTV_ASSETS_BRAND_RASTER_H
#define PS4IPTV_ASSETS_BRAND_RASTER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ui/brand_art.h"

namespace brandraster {

    struct Image {
        int w = 0, h = 0;
        std::vector<uint8_t> rgb;   // w * h * 3
    };

    inline Image render(const brandart::Mesh &mesh, int w, int h, palette::Rgba clear, int ss, bool dither) {
        Image img;
        img.w = w;
        img.h = h;
        img.rgb.assign((size_t) w * h * 3, 0);
        const int band = 32;                     // pixel rows per band: bounded memory at any size
        const int sw = w * ss;
        std::vector<float> acc;
        // per triangle: screen-space bounds, for a cheap reject per band
        struct Bounds {
            float y0, y1;
        };
        std::vector<Bounds> bounds;
        for (size_t i = 0; i + 2 < mesh.size(); i += 3) {
            bounds.push_back({std::min(mesh[i].y, std::min(mesh[i + 1].y, mesh[i + 2].y)),
                              std::max(mesh[i].y, std::max(mesh[i + 1].y, mesh[i + 2].y))});
        }
        static const int BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        for (int by = 0; by < h; by += band) {
            const int rows = std::min(band, h - by);
            const int sh = rows * ss;
            acc.assign((size_t) sw * sh * 3, 0.0f);
            for (size_t k = 0; k < (size_t) sw * sh; k++) {
                acc[k * 3] = clear.r;
                acc[k * 3 + 1] = clear.g;
                acc[k * 3 + 2] = clear.b;
            }
            for (size_t t = 0; t < bounds.size(); t++) {
                if (bounds[t].y1 < (float) by || bounds[t].y0 > (float) (by + rows)) {
                    continue;
                }
                const brandart::Vertex &a = mesh[t * 3], &b = mesh[t * 3 + 1], &c = mesh[t * 3 + 2];
                // sample space of this band
                const float ax = a.x * ss, ay = (a.y - by) * ss;
                const float bx = b.x * ss, byy = (b.y - by) * ss;
                const float cx = c.x * ss, cy = (c.y - by) * ss;
                const float area = (bx - ax) * (cy - ay) - (cx - ax) * (byy - ay);
                if (std::fabs(area) < 1e-9f) {
                    continue;
                }
                int x0 = std::max(0, (int) std::floor(std::min(ax, std::min(bx, cx))));
                int x1 = std::min(sw - 1, (int) std::ceil(std::max(ax, std::max(bx, cx))));
                int y0 = std::max(0, (int) std::floor(std::min(ay, std::min(byy, cy))));
                int y1 = std::min(sh - 1, (int) std::ceil(std::max(ay, std::max(byy, cy))));
                for (int y = y0; y <= y1; y++) {
                    const float py = (float) y + 0.5f;
                    for (int x = x0; x <= x1; x++) {
                        const float px = (float) x + 0.5f;
                        float w0 = ((bx - px) * (cy - py) - (cx - px) * (byy - py)) / area;
                        float w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) / area;
                        float w2 = 1.0f - w0 - w1;
                        if (w0 < 0 || w1 < 0 || w2 < 0) {
                            continue;
                        }
                        const float al = (w0 * a.c.a + w1 * b.c.a + w2 * c.c.a) / 255.0f;
                        if (al <= 0) {
                            continue;
                        }
                        float *d = &acc[((size_t) y * sw + x) * 3];
                        d[0] = (w0 * a.c.r + w1 * b.c.r + w2 * c.c.r) * al + d[0] * (1 - al);
                        d[1] = (w0 * a.c.g + w1 * b.c.g + w2 * c.c.g) * al + d[1] * (1 - al);
                        d[2] = (w0 * a.c.b + w1 * b.c.b + w2 * c.c.b) * al + d[2] * (1 - al);
                    }
                }
            }
            // box filter + dither + quantize
            for (int y = 0; y < rows; y++) {
                for (int x = 0; x < w; x++) {
                    float sum[3] = {0, 0, 0};
                    for (int sy = 0; sy < ss; sy++) {
                        for (int sx = 0; sx < ss; sx++) {
                            const float *s = &acc[((size_t) (y * ss + sy) * sw + x * ss + sx) * 3];
                            sum[0] += s[0];
                            sum[1] += s[1];
                            sum[2] += s[2];
                        }
                    }
                    const float off = dither ? ((float) BAYER[(by + y) & 3][x & 3] + 0.5f) / 16.0f - 0.5f : 0.0f;
                    uint8_t *o = &img.rgb[((size_t) (by + y) * w + x) * 3];
                    for (int k = 0; k < 3; k++) {
                        float v = sum[k] / (float) (ss * ss) + off;
                        o[k] = (uint8_t) std::lround(std::max(0.0f, std::min(255.0f, v)));
                    }
                }
            }
        }
        return img;
    }
}

#endif // PS4IPTV_ASSETS_BRAND_RASTER_H
