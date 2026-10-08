// Video display mode and zoom (host-testable): which mpv options the existing renderer gets.
//
// Everything here is applied with mpv_set_property_string on the one mpv handle, at runtime, without a
// reload. Option names, value syntax and runtime behaviour verified in the mpv 0.34.1 source (sha256
// 32ded8c1... = the PacBrew libmpv recipe) that the PS4 libmpv is built from:
//   keepaspect             options/options.c (vo opts)    no  = stretch the picture to the whole frame
//   panscan                options/options.c, 0.0 .. 1.0  1.0 = fill the frame, cropping the overflow
//   video-zoom             options/options.c, -20 .. 20   log2 scale: 0 = 100 %, log2(1.1) = 110 %
//   video-aspect-override  filters/f_decoder_wrapper.c    -1 = the file's own aspect (default), "16:9" / "4:3"
//                                                         (note: "no" would mean square pixels, never used)
// keepaspect / panscan / video-zoom are vo options: a change reaches vo_libmpv as VOCTRL_SET_PANSCAN and only
// recomputes the source/destination rectangles (video/out/aspect.c). video-aspect-override has UPDATE_IMGPAR:
// the decoder's image parameters are reset and the current frame redrawn (player/command.c) - playback
// position, audio and subtitle tracks are untouched. The scaling stays the same bilinear pass of the PS4
// precompiled shader set for every rectangle (gpu/video.c pass_scale_main: dscale unset, linear/sigmoid
// scaling off), so no new shader is needed.
//
// Zoom only exists while the aspect ratio is kept (aspect.c applies video-zoom in the keepaspect branch):
// in Stretch it has no effect, and the Options panel says so.

#ifndef PS4IPTV_PLAYER_DISPLAY_H
#define PS4IPTV_PLAYER_DISPLAY_H

#include <string>
#include <utility>
#include <vector>

namespace display {

    enum class Mode {
        Auto,       // original aspect ratio of the file, the whole picture visible
        Fit,        // the whole picture inside the screen, letterbox / pillarbox bars
        Fill,       // aspect kept, fills the screen, the overflowing edges cropped
        Stretch,    // fills the screen, aspect not kept (distorts)
        Aspect16x9, // displayed as 16:9
        Aspect4x3   // displayed as 4:3
    };

    const int MODE_COUNT = 6;

    // zoom steps in percent (index 0 = 100 %)
    const std::vector<int> &zoomSteps();

    // settings.json value: "auto", "fit", "fill", "stretch", "16:9", "4:3"
    const char *modeKey(Mode mode);

    Mode modeFromKey(const std::string &key);   // unknown -> Auto

    // localization key of the mode's name ("display.mode.auto" ...)
    const char *modeNameKey(Mode mode);

    // localization key of its one-line explanation ("display.mode.auto_desc" ...)
    const char *modeDescKey(Mode mode);

    // nearest valid step (unknown -> 100)
    int clampZoom(int percent);

    bool zoomAvailable(Mode mode);

    // the complete option set (every option is always set, so nothing of a previous mode remains)
    std::vector<std::pair<std::string, std::string>> mpvOptions(Mode mode, int zoomPercent);
}

#endif // PS4IPTV_PLAYER_DISPLAY_H
