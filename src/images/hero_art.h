// The Home hero's artwork (host-testable): only artwork the IPTV provider supplies, never generated, blurred,
// stretched or cropped into a fake landscape.
//
//   1. the provider's real landscape backdrop (backdrop_path), when it loads and is usable (usableBackdrop: landscape
//      >= 1.3:1, at least 640 x 300 - a portrait poster or a thumbnail sent as backdrop_path is not): full-bleed,
//      1280 x 720, drawn brighter than before (layout::home::HERO_BACKDROP_TINT / _SHADE, ~42 %) with the strong
//      left fade that keeps the text readable
//   2. otherwise the SHOWCASE: a subtle tint of the poster's own colour around it (ambient()) and the portrait poster as
//      a sharp, framed object on the right (showcase(): <= SHOWCASE_H tall, never enlarged, rounded corners, nearly
//      full brightness). The best provider image is used: the detail cover (movies: get_vod_info cover_big /
//      movie_image; series: get_series_info cover) when it differs from the list's image, else the list image
//      (stream_icon / cover). The list image is shown while the detail cover loads, and stays when it fails.
//   3. a poster under SHOWCASE_MIN_H texels, or no artwork: the clean dark hero
//   The showcase's tint (ambient / ambientArea, v7-ux-7) is subtle and lies around the poster only: a dark shade of
//   its colour behind it, eased out to the left (never into the text column) and above / below.

#ifndef PS4IPTV_IMAGES_HERO_ART_H
#define PS4IPTV_IMAGES_HERO_ART_H

namespace images {
    namespace hero {

        // a provider backdrop worth showing: landscape and large enough for the 1320 x 800 hero
        constexpr int BACKDROP_MIN_W = 640;
        constexpr int BACKDROP_MIN_H = 300;
        constexpr float BACKDROP_MIN_ASPECT = 1.3f;

        bool usableBackdrop(int w, int h);

        // the showcase poster (screen coordinates of the 1920 x 1080 Home page)
        constexpr float SHOWCASE_H = 560;        // wanted height; a HeroPoster texture (<= 512 x 768) is shrunk to it
        constexpr float SHOWCASE_MIN_H = 300;    // a smaller texture is not shown (the clean dark hero stays)
        constexpr float SHOWCASE_MAX_W = 420;    // square / wide covers: at most this wide
        constexpr float SHOWCASE_RIGHT = 1680;   // its right edge (the text column ends at 1096)
        constexpr float SHOWCASE_CENTER_Y = 395; // its vertical centre: between the navigation and the first rail

        struct Showcase {
            bool shown = false;
            float x = 0, y = 0, w = 0, h = 0;
            float scale = 0;                     // drawn / texture: <= 1 (never enlarged)
        };

        // where a poster texture of texW x texH is drawn: aspect kept, at most SHOWCASE_H tall and SHOWCASE_MAX_W wide,
        // never larger than the texture, whole pixels; not shown under SHOWCASE_MIN_H
        Showcase showcase(int texW, int texH);

        struct Rgb {
            float r = 0, g = 0, b = 0;
        };

        // the ambient tint behind the showcase (v7-ux-7: subtler, around the poster only - the v7-ux-6 field across the
        // right half read as grey fog on the TV): the poster's average colour, only 20 % desaturated (its hue stays,
        // no grey), scaled to 20 % and kept dark (no channel above AMBIENT_MAX), never darker than the page
        constexpr float AMBIENT_DESATURATE = 0.2f;
        constexpr float AMBIENT_LEVEL = 0.20f;
        constexpr float AMBIENT_MAX = 36;
        Rgb ambient(const Rgb &average, const Rgb &page);

        // where the tint lies (screen coordinates): a solid core exactly behind the poster that eases out to nothing
        // AMBIENT_SPREAD_Y above / below it, AMBIENT_SPREAD_RIGHT to its right and up to AMBIENT_SPREAD_X to its left -
        // never into the text column (x >= TEXT_RIGHT). Nothing without a shown poster.
        constexpr float AMBIENT_SPREAD_X = 220;
        constexpr float AMBIENT_SPREAD_RIGHT = 160;
        constexpr float AMBIENT_SPREAD_Y = 80;
        constexpr float TEXT_RIGHT = 1096;       // layout::home::HERO_TEXT_RIGHT
        constexpr float SCREEN_RIGHT = 1920;

        struct AmbientArea {
            bool shown = false;
            float coreX = 0, coreY = 0, coreW = 0, coreH = 0;   // solid tint: exactly behind the poster
            float fadeLeft = 0;                                 // eased to nothing over this width left of the core
            float fadeRight = 0;                                // ... this width right of it
            float fadeY = 0;                                    // ... and this height above / below it
        };

        AmbientArea ambientArea(const Showcase &poster);

        // the same around any poster rectangle: the fades stop at leftLimit / rightLimit (detail pages: the screen's
        // left edge and their text column) and at the top of the screen
        AmbientArea ambientAround(const Showcase &poster, float leftLimit, float rightLimit);

        // how much a texture of texW x texH is enlarged to cover a box of boxW x boxH (ui::Backdrop Landscape)
        float coverScale(int texW, int texH, float boxW, float boxH);

        // an episode image worth showing in its 16:9 tile (v7-ux-12: identity first, then shape). Not usable:
        //   sameAsCover   it is the series' cover (same URL)
        //   sharedBy >= 2 the same image for several episodes of the season (a repeated cover / backdrop)
        //   portrait      narrower than STILL_MIN_ASPECT (a poster, the wrong shape for a still)
        //   tiny          a side under STILL_MIN_SIDE
        // Everything else is a real still and is shown (16:9, 4:3, 5:4, square crops ...).
        constexpr float STILL_MIN_ASPECT = 0.9f;
        constexpr int STILL_MIN_SIDE = 48;
        bool episodeStillUsable(int w, int h, bool sameAsCover, int sharedBy);

        struct Inputs {
            bool backdropKnown = false;     // the provider's details name a backdrop_path
            bool backdropFailed = false;    // it could not be loaded
            bool backdropLoaded = false;    // it is in memory, with its source size:
            int backdropW = 0;
            int backdropH = 0;
            bool posterKnown = false;       // the list image (stream_icon / cover)
            bool posterFailed = false;
            bool posterLoaded = false;
            bool coverKnown = false;        // the detail cover (cover_big / movie_image / series cover), when different
            bool coverFailed = false;
            bool coverLoaded = false;
        };

        enum class Show {
            Backdrop,
            Cover,         // the showcase with the detail cover
            Poster,        // the showcase with the list image
            Nothing        // the clean dark hero (also while the first image loads)
        };

        struct Plan {
            Show show = Show::Nothing;
            bool backdropUsable = false;
            bool wantBackdrop = false;      // request the backdrop: known, not failed, not yet seen unusable
            bool wantCover = false;         // request the detail cover: no backdrop to wait for
            bool wantPoster = false;        // request the list image: no backdrop to wait for, no usable detail cover
        };

        Plan plan(const Inputs &in);
    }
}

#endif // PS4IPTV_IMAGES_HERO_ART_H
