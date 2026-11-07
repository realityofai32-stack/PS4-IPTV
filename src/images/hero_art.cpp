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

        AmbientArea ambientAround(const Showcase &p, float leftLimit, float rightLimit) {
            AmbientArea a;
            if (!p.shown || p.w <= 0 || p.h <= 0) {
                return a;
            }
            a.shown = true;
            a.coreX = p.x;
            a.coreY = p.y;
            a.coreW = p.w;
            a.coreH = p.h;
            a.fadeLeft = std::max(0.0f, std::min(AMBIENT_SPREAD_X, p.x - leftLimit));
            a.fadeRight = std::max(0.0f, std::min(AMBIENT_SPREAD_RIGHT, rightLimit - (p.x + p.w)));
            a.fadeY = std::max(0.0f, std::min(AMBIENT_SPREAD_Y, p.y));   // never above the top of the screen
            return a;
        }

        AmbientArea ambientArea(const Showcase &p) {
            return ambientAround(p, TEXT_RIGHT, SCREEN_RIGHT);
        }

        float coverScale(int texW, int texH, float boxW, float boxH) {
            if (texW <= 0 || texH <= 0) {
                return 0;
            }
            return std::max(boxW / (float) texW, boxH / (float) texH);
        }

        bool episodeStillUsable(int w, int h, bool sameAsCover, int sharedBy) {
            if (sameAsCover || sharedBy >= 2 || w < STILL_MIN_SIDE || h < STILL_MIN_SIDE) {
                return false;
            }
            return (float) w >= (float) h * STILL_MIN_ASPECT;
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
