// Video geometry (host-testable): Aspect Ratio, Crop / Fill, Zoom and Position as four independent controls,
// translated into mpv options for the existing renderer.
//
// Everything is applied with mpv_set_property_string on the one mpv handle, at runtime, without a reload.
// Option names, ranges and runtime behaviour verified in the mpv 0.34.1 source (tarball sha256 32ded8c1... =
// the PacBrew libmpv recipe; the PS4 patch touches no geometry code) and present as strings in the PS4
// libmpv.a:
//   video-aspect-override       filters/f_decoder_wrapper.c, -1 .. 10, UPDATE_IMGPAR. -1 = the file's own
//                               display aspect (default); "w:h" ratios ("16:9", "239:100") are parsed by
//                               m_option.c parse_double. ("no" would mean square pixels: never used.)
//   panscan                     options/options.c, 0.0 .. 1.0. 1.0 = fill the video area keeping the aspect,
//                               the overflow cut off (video/out/aspect.c aspect_calc_panscan)
//   video-margin-ratio-left/right/top/bottom
//                               options/options.c, 0.0 .. 1.0 of the window. With keepaspect the picture is
//                               fitted into, and clipped to, the window minus these margins (aspect.c
//                               mp_get_src_dst_rects); outside it the renderer clears to black
//   video-zoom                  options/options.c, -20 .. 20, log2 scale: 0 = 100 %, log2(1.1) = 110 %
//   video-align-x / video-align-y
//                               options/options.c, -1 .. 1: where a picture larger (or smaller) than the video
//                               area sits. -1 = left / top edge, 0 = centred, 1 = right / bottom edge. Bounded
//                               by the overflow: never shows anything beyond the picture's edge
//   video-pan-x / video-pan-y   options/options.c: reset to 0 (Position uses the align options)
//   keepaspect                  options/options.c: always yes (margins, zoom and align only exist with it)
// Not in mpv 0.34.1: video-crop (added in mpv 0.37). A crop through a video filter would rebuild the filter
// chain, so Crop is done with the margin viewport + panscan above instead:
//
//   Crop R   = a centred viewport of ratio R, as large as the screen allows (margins), filled by the picture
//              (panscan 1.0): exactly the centre R-shaped part of the displayed picture is visible, scaled up
//              as far as the screen allows. VLC's "Crop" semantics.
//   Fill     = no margins, panscan 1.0: the screen filled, the aspect kept, the overflow cut off.
//
// The vo options (everything but the aspect) reach vo_libmpv as VOCTRL_SET_PANSCAN (video/out/vo.c
// update_opts) and only recompute the source / destination rectangles before the next frame. The aspect has
// UPDATE_IMGPAR: the decoder's output parameters are updated and the current frame redrawn (player/command.c).
// Playback position, pause state, audio and subtitle tracks are untouched by either. The scaling stays the same
// bilinear pass of the PS4 precompiled shader set for every rectangle, so no new shader is needed.

#ifndef PS4IPTV_PLAYER_DISPLAY_H
#define PS4IPTV_PLAYER_DISPLAY_H

#include <string>
#include <utility>
#include <vector>

namespace display {

    // the display aspect the picture is shown with (a ratio, never a crop)
    enum class Aspect {
        Source,     // the file's own (default)
        R16x9,
        R16x10,
        R4x3,
        R5x4,
        R1x1,
        R185,       // 1.85:1
        R221,       // 2.21:1
        R235,       // 2.35:1
        R239,       // 2.39:1
        R240        // 2.40:1
    };

    const int ASPECT_COUNT = 11;

    // what is cut off: nothing (default), whatever does not fit the screen, or everything outside a ratio
    enum class Crop {
        None,
        Fill,
        R16x9,
        R16x10,
        R4x3,
        R1x1,
        R185,
        R221,
        R235,
        R239,
        R240
    };

    const int CROP_COUNT = 11;

    // Position steps: -POSITION_STEPS (left / top edge) .. 0 (centred) .. POSITION_STEPS (right / bottom edge)
    const int POSITION_STEPS = 4;

    struct Geometry {
        Aspect aspect = Aspect::Source;
        Crop crop = Crop::None;
        int zoom = 100;     // percent, one of zoomSteps()
        int posX = 0;       // -POSITION_STEPS .. POSITION_STEPS
        int posY = 0;

        bool operator==(const Geometry &o) const {
            return aspect == o.aspect && crop == o.crop && zoom == o.zoom && posX == o.posX && posY == o.posY;
        }

        bool operator!=(const Geometry &o) const { return !(*this == o); }
    };

    // Auto / Source, no crop, 100 %, centred
    Geometry defaults();

    bool isDefault(const Geometry &g);

    // zoom steps in percent (index 0 = 100 %)
    const std::vector<int> &zoomSteps();

    // nearest valid step (unknown -> 100)
    int clampZoom(int percent);

    int clampPosition(int step);

    // the next / previous value (wraps for aspect and crop; zoom and position stop at their ends)
    Aspect stepAspect(Aspect a, int delta);

    Crop stepCrop(Crop c, int delta);

    int stepZoom(int percent, int delta);

    int stepPosition(int step, int delta);

    // settings.json values: "source", "16:9" ... / "none", "fill", "16:9" ...
    const char *aspectKey(Aspect a);

    Aspect aspectFromKey(const std::string &key);   // unknown -> Source

    const char *cropKey(Crop c);

    Crop cropFromKey(const std::string &key);       // unknown -> None

    // display ratio (width / height) of a fixed aspect / crop (0 for Source / None / Fill)
    double aspectRatio(Aspect a);

    double cropRatio(Crop c);

    // the ratio as shown to the user ("16:9", "2.39:1"); "" for Source / None / Fill (localized names)
    std::string aspectLabel(Aspect a);

    std::string cropLabel(Crop c);

    // mpv "video-aspect-override" value: "-1" (the file's own) or an integer ratio ("239:100")
    std::string aspectOption(Aspect a);

    // settings.json written before Aspect Ratio and Crop were separate had one "displayMode" ("auto", "fit", "fill", "stretch", "16:9", "4:3");
    // its meaning as aspect + crop. "stretch" on the 16:9 screen = the picture shown as 16:9.
    void migrateDisplayMode(const std::string &mode, Aspect &aspect, Crop &crop);

    // margins (fractions of the window) of the centred viewport of ratio `ratio` on a windowW x windowH screen
    struct Margins {
        double left = 0, right = 0, top = 0, bottom = 0;
    };

    Margins viewport(double ratio, int windowW, int windowH);

    // the complete option set for a window of windowW x windowH pixels (every option is always set, so nothing
    // of a previous geometry remains)
    std::vector<std::pair<std::string, std::string>> mpvOptions(const Geometry &g, int windowW, int windowH);
}

#endif // PS4IPTV_PLAYER_DISPLAY_H
