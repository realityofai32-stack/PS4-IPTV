#include <algorithm>
#include <cmath>

#include "hero_art.h"

namespace images {
    namespace hero {

        bool usableBackdrop(int w, int h) {
            return w >= BACKDROP_MIN_W && h >= BACKDROP_MIN_H && (float) w >= (float) h * BACKDROP_MIN_ASPECT;
        }

        Showcase showcase(int texW, int texH) {
            Showcase s;
            if (texW <= 0 || texH <= 0 || (float) texH < SHOWCASE_MIN_H) {
                return s;
            }
            float scale = std::min({1.0f, SHOWCASE_H / (float) texH, SHOWCASE_MAX_W / (float) texW});
            s.w = std::round((float) texW * scale);
            s.h = std::round((float) texH * scale);
            s.scale = scale;
            s.x = SHOWCASE_RIGHT - s.w;
            s.y = std::round(SHOWCASE_CENTER_Y - s.h / 2);
            s.shown = true;
            return s;
        }

        Rgb ambient(const Rgb &avg, const Rgb &page) {
            float luma = 0.2126f * avg.r + 0.7152f * avg.g + 0.0722f * avg.b;
            Rgb c;
            c.r = (avg.r + (luma - avg.r) * AMBIENT_DESATURATE) * AMBIENT_LEVEL;
            c.g = (avg.g + (luma - avg.g) * AMBIENT_DESATURATE) * AMBIENT_LEVEL;
            c.b = (avg.b + (luma - avg.b) * AMBIENT_DESATURATE) * AMBIENT_LEVEL;
            float top = std::max({c.r, c.g, c.b});
            if (top > AMBIENT_MAX) {
                float k = AMBIENT_MAX / top;   // keep the hue, cap the brightness
                c.r *= k;
                c.g *= k;
                c.b *= k;
            }
            c.r = std::round(std::max(c.r, page.r));
            c.g = std::round(std::max(c.g, page.g));
            c.b = std::round(std::max(c.b, page.b));
            return c;
        }

        Plan plan(const Inputs &in) {
            Plan p;
            p.backdropUsable = in.backdropKnown && !in.backdropFailed && in.backdropLoaded
                               && usableBackdrop(in.backdropW, in.backdropH);
            p.wantBackdrop = in.backdropKnown && !in.backdropFailed && (!in.backdropLoaded || p.backdropUsable);
            const bool cover = in.coverKnown && !in.coverFailed;
            const bool poster = in.posterKnown && !in.posterFailed;
            const bool showcaseWanted = !p.backdropUsable && !p.wantBackdrop;
            p.wantCover = showcaseWanted && cover;
            p.wantPoster = showcaseWanted && !cover && poster;
            if (p.backdropUsable) {
                p.show = Show::Backdrop;
            } else if (cover && in.coverLoaded) {
                p.show = Show::Cover;
            } else if (poster && in.posterLoaded) {
                p.show = Show::Poster;   // also while the detail cover / a backdrop loads, when it is in memory
            }
            return p;
        }
    }
}
