#include "hero_art.h"

namespace images {
    namespace hero {

        bool usableBackdrop(int w, int h) {
            return w >= BACKDROP_MIN_W && h >= BACKDROP_MIN_H && (float) w >= (float) h * BACKDROP_MIN_ASPECT;
        }

        Plan plan(const Inputs &in) {
            Plan p;
            p.backdropUsable = in.backdropKnown && !in.backdropFailed && in.backdropLoaded
                               && usableBackdrop(in.backdropW, in.backdropH);
            p.wantBackdrop = in.backdropKnown && !in.backdropFailed && (!in.backdropLoaded || p.backdropUsable);
            p.wantPoster = !p.backdropUsable && !p.wantBackdrop && in.posterKnown;
            if (p.backdropUsable) {
                p.show = Show::Backdrop;
            } else if (in.posterKnown && in.posterLoaded) {
                p.show = Show::Poster;   // also while a backdrop loads, when the poster is still in memory
            }
            return p;
        }
    }
}
