// The Home hero's artwork choice (host-testable): only artwork the IPTV provider supplies, never generated.
//
//   1. the provider's real landscape backdrop (backdrop_path), when it loads and is usable (usableBackdrop: landscape
//      and large enough - a portrait poster or a thumbnail sent as backdrop_path is not) - drawn as before, 1280 x 720
//   2. otherwise the dark V7 hero with a subtle field of the poster's average colour and the sharp portrait poster
//      on the right (ImageKind::HeroPoster, ui::Backdrop Poster art): never stretched, never enlarged past
//      layout::home::HERO_POSTER_MAX_UPSCALE; a poster too small for that is omitted (the clean dark hero stays)
//   3. no artwork: the clean dark hero
// While a backdrop is loading only the backdrop is requested (one large texture at a time); the poster stays on
// screen meanwhile if it is still in memory.
// (The v7-ux checkpoint's generated blurred background was removed after the hardware test: it looked bad.)

#ifndef PS4IPTV_IMAGES_HERO_ART_H
#define PS4IPTV_IMAGES_HERO_ART_H

namespace images {
    namespace hero {

        // a provider backdrop worth showing: landscape and large enough for the 1320 x 800 hero
        constexpr int BACKDROP_MIN_W = 640;
        constexpr int BACKDROP_MIN_H = 300;
        constexpr float BACKDROP_MIN_ASPECT = 1.3f;

        bool usableBackdrop(int w, int h);

        struct Inputs {
            bool backdropKnown = false;     // the provider's details name a backdrop_path
            bool backdropFailed = false;    // it could not be loaded
            bool backdropLoaded = false;    // it is in memory, with its source size:
            int backdropW = 0;
            int backdropH = 0;
            bool posterKnown = false;       // the item has a poster / cover
            bool posterLoaded = false;      // the hero poster is in memory
        };

        enum class Show {
            Backdrop,
            Poster,        // the sharp poster on the right with its colour field (ui::Backdrop decides its size)
            Nothing        // the clean dark hero (also while the first image loads)
        };

        struct Plan {
            Show show = Show::Nothing;
            bool backdropUsable = false;
            bool wantBackdrop = false;      // request the backdrop: known, not failed, not yet seen unusable
            bool wantPoster = false;        // request the hero poster: no backdrop to wait for
        };

        Plan plan(const Inputs &in);
    }
}

#endif // PS4IPTV_IMAGES_HERO_ART_H
