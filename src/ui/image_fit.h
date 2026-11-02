// Artwork presentation rules: which part of an image is drawn where (host-testable, no libcross2d).
//
// Every widget that shows artwork asks here instead of computing its own crop:
//   Cover     fill the box, crop the longer side at an anchor        landscape backdrops, episode stills
//   Contain   the whole image inside the box, centred, never cropped
//   Poster    a portrait poster: fills a box of (nearly) its own shape with at most a sliver cropped, otherwise
//             contained - never forced into a 16:9 crop
//   Logo      Contain inside a padding with an upscale limit         channel logos are never cropped
// posterBackdrop() places a portrait poster as ambient art in a wide box (no real landscape image): ratio kept,
// pushed to the right, cropped by the box - never stretched to the box.
//
// All results are in floats; pixelSnap() / texel rounding happen at the widget. Invalid sizes (0, negative, NaN)
// give valid = false: nothing is drawn, nothing divides by zero.

#ifndef PS4IPTV_UI_IMAGE_FIT_H
#define PS4IPTV_UI_IMAGE_FIT_H

namespace fit {

    enum class Mode {
        Cover,
        Contain,
        Poster,
        Logo
    };

    struct Rect {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;

        bool empty() const { return !(w > 0) || !(h > 0); }
    };

    struct Placement {
        Rect src;            // texels of the image: 0..imageW x 0..imageH
        Rect dst;            // box coordinates (may be smaller than the box: Contain / Logo / Poster)
        bool valid = false;  // false: nothing to draw
    };

    struct Options {
        float anchorX = 0.5f;          // Cover: the kept part of a cropped width (0 left .. 1 right)
        float anchorY = 0.5f;          // Cover: the kept part of a cropped height (0 top .. 1 bottom)
        float padding = 0;             // Contain / Logo: inset on every side
        float maxUpscale = 0;          // Contain / Logo: largest scale factor (0 = unlimited)
        float posterTolerance = 0.15f; // Poster: relative aspect difference that is still filled
    };

    Placement place(Mode mode, float imageW, float imageH, float boxW, float boxH, const Options &options = Options());

    // the part of a placement inside the box 0..boxW x 0..boxH, its source cropped in proportion
    Placement clip(const Placement &placement, float boxW, float boxH);

    // A portrait poster as ambient art in a wide box (hero / detail backdrop without a landscape image): drawn at
    // heightScale x the box height with its own aspect ratio, its right edge rightBleed px beyond the box's right
    // edge, the vertical overflow split by anchorY (0: the top stays, 1: the bottom stays); clipped to the box.
    Placement posterBackdrop(float imageW, float imageH, float boxW, float boxH, float heightScale, float rightBleed,
                             float anchorY);

    // whole pixels, edges rounded (no gap and no overlap between neighbours)
    Rect pixelSnap(const Rect &r);
}

#endif // PS4IPTV_UI_IMAGE_FIT_H
