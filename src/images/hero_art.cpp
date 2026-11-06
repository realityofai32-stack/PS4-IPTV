#include <algorithm>
#include <cmath>
#include <cstring>

#include "hero_art.h"

namespace images {
    namespace hero {

        namespace {
            // 4 x 4 Bayer matrix, centred: -7.5 .. 7.5 sixteenths of one 8-bit level
            const float BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

            struct Rgbf {
                float r, g, b;
            };

            // area average of the source rectangle (x0, y0, w, h) into ow x oh float pixels (alpha-weighted, opaque
            // result: a transparent poster corner becomes black, never a coloured fringe)
            std::vector<Rgbf> areaSample(const Image &src, int x0, int y0, int w, int h, int ow, int oh) {
                std::vector<Rgbf> out((size_t) ow * (size_t) oh, {0, 0, 0});
                for (int oy = 0; oy < oh; oy++) {
                    int sy0 = y0 + (int) ((int64_t) oy * h / oh);
                    int sy1 = std::max(sy0 + 1, y0 + (int) ((int64_t) (oy + 1) * h / oh));
                    for (int ox = 0; ox < ow; ox++) {
                        int sx0 = x0 + (int) ((int64_t) ox * w / ow);
                        int sx1 = std::max(sx0 + 1, x0 + (int) ((int64_t) (ox + 1) * w / ow));
                        double r = 0, g = 0, b = 0;
                        int n = 0;
                        for (int sy = sy0; sy < sy1 && sy < src.h; sy++) {
                            const uint8_t *p = &src.rgba[((size_t) sy * (size_t) src.w + (size_t) sx0) * 4];
                            for (int sx = sx0; sx < sx1 && sx < src.w; sx++, p += 4) {
                                double a = p[3] / 255.0;
                                r += p[0] * a;
                                g += p[1] * a;
                                b += p[2] * a;
                                n++;
                            }
                        }
                        if (n > 0) {
                            out[(size_t) oy * (size_t) ow + (size_t) ox] = {(float) (r / n), (float) (g / n),
                                                                           (float) (b / n)};
                        }
                    }
                }
                return out;
            }

            // one box blur pass along x or y, clamped at the edges (running sum: O(pixels) whatever the radius)
            void boxPass(std::vector<Rgbf> &img, int w, int h, int radius, bool horizontal) {
                const int len = horizontal ? w : h;
                const int lines = horizontal ? h : w;
                std::vector<Rgbf> line((size_t) len);
                const float inv = 1.0f / (float) (2 * radius + 1);
                for (int l = 0; l < lines; l++) {
                    auto at = [&](int i) -> Rgbf & {
                        return horizontal ? img[(size_t) l * (size_t) w + (size_t) i] : img[(size_t) i * (size_t) w + (size_t) l];
                    };
                    for (int i = 0; i < len; i++) {
                        line[(size_t) i] = at(i);
                    }
                    Rgbf sum = {0, 0, 0};
                    for (int k = -radius; k <= radius; k++) {
                        const Rgbf &p = line[(size_t) std::min(std::max(k, 0), len - 1)];
                        sum.r += p.r;
                        sum.g += p.g;
                        sum.b += p.b;
                    }
                    for (int i = 0; i < len; i++) {
                        at(i) = {sum.r * inv, sum.g * inv, sum.b * inv};
                        const Rgbf &out = line[(size_t) std::max(i - radius, 0)];
                        const Rgbf &in = line[(size_t) std::min(i + radius + 1, len - 1)];
                        sum.r += in.r - out.r;
                        sum.g += in.g - out.g;
                        sum.b += in.b - out.b;
                    }
                }
            }

            float smooth(float t) {
                t = std::min(1.0f, std::max(0.0f, t));
                return t * t * (3 - 2 * t);
            }

            uint8_t dithered(float v, float d) {
                float q = std::floor(v + d);
                return (uint8_t) std::min(255.0f, std::max(0.0f, q));
            }

            // bilinear enlargement of a small float image to ow x oh RGBA8, ordered dither
            Image enlarge(const std::vector<Rgbf> &small, int w, int h, int ow, int oh) {
                Image out;
                out.w = ow;
                out.h = oh;
                out.rgba.resize((size_t) ow * (size_t) oh * 4);
                for (int y = 0; y < oh; y++) {
                    float fy = std::max(0.0f, ((float) y + 0.5f) * (float) h / (float) oh - 0.5f);
                    int y0 = std::min((int) fy, h - 1);
                    int y1 = std::min(y0 + 1, h - 1);
                    float ty = fy - (float) y0;
                    uint8_t *row = &out.rgba[(size_t) y * (size_t) ow * 4];
                    for (int x = 0; x < ow; x++) {
                        float fx = std::max(0.0f, ((float) x + 0.5f) * (float) w / (float) ow - 0.5f);
                        int x0 = std::min((int) fx, w - 1);
                        int x1 = std::min(x0 + 1, w - 1);
                        float tx = fx - (float) x0;
                        const Rgbf &a = small[(size_t) y0 * (size_t) w + (size_t) x0];
                        const Rgbf &b = small[(size_t) y0 * (size_t) w + (size_t) x1];
                        const Rgbf &c = small[(size_t) y1 * (size_t) w + (size_t) x0];
                        const Rgbf &d = small[(size_t) y1 * (size_t) w + (size_t) x1];
                        float top, bottom;
                        float dither = (BAYER[y & 3][x & 3] + 0.5f) / 16.0f;
                        top = a.r + (b.r - a.r) * tx;
                        bottom = c.r + (d.r - c.r) * tx;
                        row[x * 4 + 0] = dithered(top + (bottom - top) * ty, dither);
                        top = a.g + (b.g - a.g) * tx;
                        bottom = c.g + (d.g - c.g) * tx;
                        row[x * 4 + 1] = dithered(top + (bottom - top) * ty, dither);
                        top = a.b + (b.b - a.b) * tx;
                        bottom = c.b + (d.b - c.b) * tx;
                        row[x * 4 + 2] = dithered(top + (bottom - top) * ty, dither);
                        row[x * 4 + 3] = 255;
                    }
                }
                return out;
            }

            // desaturate, darken, and darker towards the left (the hero's text column)
            void grade(std::vector<Rgbf> &img, int w, int h) {
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        Rgbf &p = img[(size_t) y * (size_t) w + (size_t) x];
                        float luma = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
                        float ramp = LEFT_DARKEN + (1 - LEFT_DARKEN) * smooth(((float) x + 0.5f) / ((float) w * LEFT_RAMP_END));
                        float k = DARKEN * ramp;
                        p.r = (p.r + (luma - p.r) * DESATURATE) * k;
                        p.g = (p.g + (luma - p.g) * DESATURATE) * k;
                        p.b = (p.b + (luma - p.b) * DESATURATE) * k;
                    }
                }
            }
        }

        bool usableBackdrop(int w, int h) {
            return w >= BACKDROP_MIN_W && h >= BACKDROP_MIN_H && (float) w >= (float) h * BACKDROP_MIN_ASPECT;
        }

        Plan plan(const Inputs &in) {
            Plan p;
            p.backdropUsable = in.backdropKnown && !in.backdropFailed && in.backdropLoaded
                               && usableBackdrop(in.backdropW, in.backdropH);
            p.wantBackdrop = in.backdropKnown && !in.backdropFailed && (!in.backdropLoaded || p.backdropUsable);
            p.wantGenerated = !p.backdropUsable && in.fallbackKnown;
            if (p.backdropUsable) {
                p.show = Show::Backdrop;
            } else if (in.fallbackKnown && in.generatedLoaded) {
                p.show = Show::Generated;
            }
            return p;
        }

        const char *modeName(Mode m) {
            switch (m) {
                case Mode::Landscape:
                    return "wide cover (sharp)";
                case Mode::Blurred:
                    return "blurred poster";
                default:
                    return "ambient only";
            }
        }

        const char *cropName(Crop c) {
            switch (c) {
                case Crop::Band:
                    return "16:9 band";
                case Crop::Center:
                    return "16:9 centre";
                case Crop::Full:
                    return "full";
                default:
                    return "none";
            }
        }

        Result generate(const Image &src) {
            Result r;
            if (!src.valid()) {
                return r;
            }
            r.sourceW = src.w;
            r.sourceH = src.h;
            r.ambient = averageColor(src);
            const float aspect = (float) src.w / (float) src.h;
            const float target = 16.0f / 9.0f;

            if (std::min(src.w, src.h) < AMBIENT_MAX_SIDE) {
                // too small to be recognisable: a clean dark wash of its colour, brighter at the right
                r.mode = Mode::Ambient;
                float lum = 0.2126f * r.ambient.r + 0.7152f * r.ambient.g + 0.0722f * r.ambient.b;
                Rgbf base = {r.ambient.r + (lum - r.ambient.r) * DESATURATE, r.ambient.g + (lum - r.ambient.g) * DESATURATE,
                             r.ambient.b + (lum - r.ambient.b) * DESATURATE};
                const int w = 16, h = 9;
                std::vector<Rgbf> wash((size_t) w * h);
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        float k = 0.15f + 0.45f * smooth((float) x / (float) (w - 1)) * (1 - 0.35f * std::fabs((float) y / (h - 1) - 0.35f));
                        wash[(size_t) y * w + (size_t) x] = {base.r * k, base.g * k, base.b * k};
                    }
                }
                r.image = enlarge(wash, w, h, AMBIENT_W, AMBIENT_H);
                return r;
            }

            // the 16:9 part of the image that is used
            int cw, ch, cx, cy;
            if (std::fabs(aspect - target) < 0.01f) {
                r.crop = Crop::Full;
                cw = src.w;
                ch = src.h;
                cx = cy = 0;
            } else if (aspect < target) {
                r.crop = Crop::Band;
                cw = src.w;
                ch = std::max(1, (int) std::lround(src.w / target));
                cx = 0;
                cy = (int) std::lround(src.h * BAND_CENTER - ch / 2.0);
                cy = std::min(std::max(cy, 0), src.h - ch);
            } else {
                r.crop = Crop::Center;
                ch = src.h;
                cw = std::max(1, (int) std::lround(src.h * target));
                cy = 0;
                cx = (src.w - cw) / 2;
            }
            r.cropX = cx;
            r.cropY = cy;
            r.cropW = cw;
            r.cropH = ch;

            if (aspect >= LANDSCAPE_ASPECT && src.w >= LANDSCAPE_MIN_W) {
                // a large wide cover is real landscape art: sharp, never enlarged on the CPU
                r.mode = Mode::Landscape;
                int ow = std::min(cw, 1280);
                int oh = std::max(1, (int) std::lround(ow / target));
                std::vector<Rgbf> px = areaSample(src, cx, cy, cw, ch, ow, oh);
                Image out;
                out.w = ow;
                out.h = oh;
                out.rgba.resize((size_t) ow * (size_t) oh * 4);
                for (size_t i = 0; i < px.size(); i++) {
                    out.rgba[i * 4 + 0] = (uint8_t) std::min(255.0f, px[i].r + 0.5f);
                    out.rgba[i * 4 + 1] = (uint8_t) std::min(255.0f, px[i].g + 0.5f);
                    out.rgba[i * 4 + 2] = (uint8_t) std::min(255.0f, px[i].b + 0.5f);
                    out.rgba[i * 4 + 3] = 255;
                }
                r.image = std::move(out);
                return r;
            }

            r.mode = Mode::Blurred;
            std::vector<Rgbf> work = areaSample(src, cx, cy, cw, ch, WORK_W, WORK_H);
            for (int pass = 0; pass < 3; pass++) {
                boxPass(work, WORK_W, WORK_H, BLUR_RADIUS, true);
                boxPass(work, WORK_W, WORK_H, BLUR_RADIUS, false);
            }
            grade(work, WORK_W, WORK_H);
            r.image = enlarge(work, WORK_W, WORK_H, OUT_W, OUT_H);
            return r;
        }
    }
}
