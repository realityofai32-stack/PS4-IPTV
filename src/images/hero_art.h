// The Home hero's background when the provider has no usable landscape backdrop (host-testable, CPU only, no new
// library): a cinematic 16:9 image generated ONCE on a worker from the poster / cover the provider already sends,
// cached by the image loader as an ordinary ready-to-draw texture (ImageKind::HeroAmbient).
//
// Artwork priority on Home (screens/home_screen.cpp):
//   1. the provider's real landscape backdrop (backdrop_path), when it loads and is usable (usableBackdrop:
//      landscape and large enough) - drawn as before (1280 x 720)
//   2. generate(): from the poster / cover
//        Landscape  the cover is itself a large wide image (>= LANDSCAPE_MIN_W, aspect >= LANDSCAPE_ASPECT):
//                   cover-cropped to 16:9, no blur, at most 1280 x 720 (never enlarged on the CPU)
//        Blurred    otherwise: a 16:9 crop (a band at BAND_CENTER of a portrait poster, the centre of a wide one),
//                   area-averaged to WORK_W x WORK_H, blurred (3 box passes of BLUR_RADIUS = a near-Gaussian, strong
//                   enough to hide low-resolution poster artefacts), moderately desaturated and darkened, darker towards
//                   the left text column, then smoothly enlarged to OUT_W x OUT_H with an ordered dither (no banding in
//                   the dark gradients on the TV)
//   3. Ambient     the poster is too small to be recognisable even blurred (< AMBIENT_MAX_SIDE): no picture at all, a
//                  clean dark wash of its average colour (AMBIENT_W x AMBIENT_H)
//   A poster that cannot be loaded / decoded at all: the plain dark hero (the Backdrop's faint glow).
// Cost (measured by the host tests, logged on the console): a few ms per poster on the worker; 900 KiB of texture
// (640 x 360 RGBA), 225 KiB for Ambient, 3.5 MiB at most for a Landscape cover. Nothing is done per frame.

#ifndef PS4IPTV_IMAGES_HERO_ART_H
#define PS4IPTV_IMAGES_HERO_ART_H

#include "image_decode.h"

namespace images {
    namespace hero {

        constexpr int OUT_W = 640;
        constexpr int OUT_H = 360;
        constexpr int WORK_W = 160;
        constexpr int WORK_H = 90;
        constexpr int BLUR_RADIUS = 4;          // x 3 passes at 160 x 90 ~ sigma 4.5 px = ~37 px on the 1320 px hero
        constexpr int AMBIENT_W = 320;
        constexpr int AMBIENT_H = 180;
        constexpr int AMBIENT_MAX_SIDE = 80;    // a poster whose shorter side is below this is not shown even blurred
        constexpr int LANDSCAPE_MIN_W = 960;
        constexpr float LANDSCAPE_ASPECT = 1.5f;
        constexpr float BAND_CENTER = 0.32f;    // a portrait poster's 16:9 band: centred at 32 % of its height
        constexpr float DESATURATE = 0.30f;
        constexpr float DARKEN = 0.95f;
        constexpr float LEFT_DARKEN = 0.70f;    // brightness at the left edge (ramps to 1 at LEFT_RAMP_END); ui::Backdrop adds its own fade
        constexpr float LEFT_RAMP_END = 0.55f;

        // a provider backdrop worth showing sharp: landscape and large enough for the 1320 x 800 hero
        constexpr int BACKDROP_MIN_W = 640;
        constexpr int BACKDROP_MIN_H = 300;
        constexpr float BACKDROP_MIN_ASPECT = 1.3f;

        bool usableBackdrop(int w, int h);

        enum class Mode {
            Landscape,   // a large wide cover, cropped to 16:9, sharp
            Blurred,     // the cinematic blur of the poster
            Ambient      // the poster's average colour only
        };

        enum class Crop {
            None,        // Ambient
            Band,        // a horizontal 16:9 band of a taller image
            Center,      // the centre of a wider image
            Full         // already 16:9
        };

        struct Result {
            Mode mode = Mode::Ambient;
            Crop crop = Crop::None;
            Image image;                 // the texture to upload (always valid for a valid source)
            Average ambient;             // the source's average colour
            int sourceW = 0;             // the image generate() was given
            int sourceH = 0;
            int cropX = 0, cropY = 0, cropW = 0, cropH = 0;   // the part of it used (Blurred / Landscape)
        };

        // src: the decoded poster (any size; the loader passes it fitted into 1280 x 1280). An invalid source gives
        // an invalid image (the caller shows the plain dark hero).
        Result generate(const Image &src);

        // ---- which image the hero shows (Home binds and requests from this; host-tested with a simulated loader)
        struct Inputs {
            bool backdropKnown = false;     // the provider's details name a backdrop_path
            bool backdropFailed = false;    // it could not be loaded
            bool backdropLoaded = false;    // it is in memory, with its source size:
            int backdropW = 0;
            int backdropH = 0;
            bool fallbackKnown = false;     // there is a poster / cover to generate from
            bool generatedLoaded = false;   // its generated background is in memory
        };

        enum class Show {
            Backdrop,
            Generated,
            Nothing        // the plain dark hero (also while the first image loads)
        };

        struct Plan {
            Show show = Show::Nothing;
            bool backdropUsable = false;
            bool wantBackdrop = false;      // request (or keep) the backdrop: known, not failed, not yet seen unusable
            bool wantGenerated = false;     // request (or keep) the generated background: no usable backdrop (yet)
        };

        Plan plan(const Inputs &in);

        const char *modeName(Mode m);

        const char *cropName(Crop c);
    }
}

#endif // PS4IPTV_IMAGES_HERO_ART_H
