#include <algorithm>
#include <cmath>

#include "image_fit.h"

namespace fit {

    namespace {
        bool positive(float v) {
            return v > 0 && std::isfinite(v);
        }

        float clamp01(float v) {
            return std::isfinite(v) ? std::min(1.0f, std::max(0.0f, v)) : 0.5f;
        }

        Placement cover(float iw, float ih, float bw, float bh, const Options &o) {
            Placement p;
            p.valid = true;
            p.dst = {0, 0, bw, bh};
            p.src = {0, 0, iw, ih};
            float boxAspect = bw / bh;
            if (iw / ih > boxAspect) {
                p.src.w = ih * boxAspect;                       // wider image: crop the sides
                p.src.x = (iw - p.src.w) * clamp01(o.anchorX);
            } else {
                p.src.h = iw / boxAspect;                       // taller image: crop top / bottom
                p.src.y = (ih - p.src.h) * clamp01(o.anchorY);
            }
            return p;
        }

        Placement contain(float iw, float ih, float bw, float bh, const Options &o) {
            Placement p;
            float pad = positive(o.padding) ? o.padding : 0;
            float aw = bw - 2 * pad;
            float ah = bh - 2 * pad;
            if (!positive(aw) || !positive(ah)) {
                return p;   // the padding leaves no room
            }
            float scale = std::min(aw / iw, ah / ih);
            if (positive(o.maxUpscale)) {
                scale = std::min(scale, o.maxUpscale);
            }
            p.valid = true;
            p.src = {0, 0, iw, ih};
            p.dst.w = iw * scale;
            p.dst.h = ih * scale;
            p.dst.x = (bw - p.dst.w) / 2;
            p.dst.y = (bh - p.dst.h) / 2;
            return p;
        }
    }

    Placement place(Mode mode, float iw, float ih, float bw, float bh, const Options &o) {
        if (!positive(iw) || !positive(ih) || !positive(bw) || !positive(bh)) {
            return Placement();
        }
        switch (mode) {
            case Mode::Cover:
                return cover(iw, ih, bw, bh, o);
            case Mode::Poster: {
                // a poster in a box of (nearly) its shape fills it - a few % cropped at most; anything else is
                // shown whole (a landscape still in a poster tile, a portrait poster in a wide box)
                float relative = (iw / ih) / (bw / bh);
                float tolerance = positive(o.posterTolerance) ? o.posterTolerance : 0;
                if (std::fabs(relative - 1.0f) < tolerance) {
                    return cover(iw, ih, bw, bh, o);
                }
                Options inside = o;
                inside.padding = 0;
                return contain(iw, ih, bw, bh, inside);
            }
            case Mode::Logo:
            case Mode::Contain:
                return contain(iw, ih, bw, bh, o);
        }
        return Placement();
    }

    Placement clip(const Placement &p, float bw, float bh) {
        if (!p.valid || p.dst.empty() || p.src.empty() || !positive(bw) || !positive(bh)) {
            return Placement();
        }
        float x0 = std::max(0.0f, p.dst.x);
        float y0 = std::max(0.0f, p.dst.y);
        float x1 = std::min(bw, p.dst.x + p.dst.w);
        float y1 = std::min(bh, p.dst.y + p.dst.h);
        if (!(x1 > x0) || !(y1 > y0)) {
            return Placement();   // entirely outside the box
        }
        float sx = p.src.w / p.dst.w;   // texels per box unit
        float sy = p.src.h / p.dst.h;
        Placement out;
        out.valid = true;
        out.dst = {x0, y0, x1 - x0, y1 - y0};
        out.src = {p.src.x + (x0 - p.dst.x) * sx, p.src.y + (y0 - p.dst.y) * sy, (x1 - x0) * sx, (y1 - y0) * sy};
        return out;
    }

    Placement posterBackdrop(float iw, float ih, float bw, float bh, float heightScale, float rightBleed,
                             float anchorY) {
        if (!positive(heightScale)) {
            return Placement();
        }
        PosterArt spec;
        spec.height = bh * heightScale;
        spec.rightBleed = rightBleed;
        spec.anchorY = anchorY;
        return posterArt(iw, ih, bw, bh, spec).placement;
    }

    PosterArtPlacement posterArt(float iw, float ih, float bw, float bh, const PosterArt &spec) {
        PosterArtPlacement out;
        if (!positive(iw) || !positive(ih) || !positive(bw) || !positive(bh) || !positive(spec.height)) {
            return out;
        }
        float h = spec.height;
        if (positive(spec.maxUpscale) && h > ih * spec.maxUpscale) {
            h = ih * spec.maxUpscale;
            out.limited = true;
        }
        if (positive(spec.maxWidth) && h * iw / ih > spec.maxWidth) {
            h = spec.maxWidth * ih / iw;
            out.limited = true;
        }
        if (positive(spec.minHeight) && h < spec.minHeight) {
            return out;   // too small to be art: the hero stays black
        }
        Placement p;
        p.valid = true;
        p.src = {0, 0, iw, ih};
        p.dst.h = h;
        p.dst.w = h * iw / ih;    // the poster's own ratio: never stretched to the box
        p.dst.x = bw + (std::isfinite(spec.rightBleed) ? spec.rightBleed : 0) - p.dst.w;
        p.dst.y = -(h - bh) * clamp01(spec.anchorY);   // a shorter poster: the room above it, by the anchor
        out.drawn = p.dst;
        out.scale = h / ih;
        out.placement = clip(p, bw, bh);
        return out;
    }

    Rgb ambientWash(const Rgb &avg, const Rgb &tint, const Rgb &shade, float shadeAlpha, const Rgb &page,
                    float level, float desaturate, float maxChannel) {
        const float a = clamp01(shadeAlpha);
        // the average as it reaches the screen: texture modulation, then the shade on top
        Rgb d = {avg.r * tint.r / 255 * (1 - a) + shade.r * a, avg.g * tint.g / 255 * (1 - a) + shade.g * a,
                 avg.b * tint.b / 255 * (1 - a) + shade.b * a};
        const float y = 0.2126f * d.r + 0.7152f * d.g + 0.0722f * d.b;
        const float s = clamp01(desaturate);
        const float l = clamp01(level);
        auto channel = [&](float c, float p) {
            float grey = c + (y - c) * s;
            float v = std::max(p, p + (grey - p) * l);   // never darker than the page: no dark patch
            return std::isfinite(maxChannel) && maxChannel > 0 ? std::min(v, std::max(maxChannel, p)) : v;
        };
        return {channel(d.r, page.r), channel(d.g, page.g), channel(d.b, page.b)};
    }

    Rect pixelSnap(const Rect &r) {
        float x0 = std::round(r.x);
        float y0 = std::round(r.y);
        return {x0, y0, std::round(r.x + r.w) - x0, std::round(r.y + r.h) - y0};
    }
}
