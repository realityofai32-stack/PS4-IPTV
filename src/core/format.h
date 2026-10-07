// Display formatting for times and durations (host-testable).

#ifndef PS4IPTV_CORE_FORMAT_H
#define PS4IPTV_CORE_FORMAT_H

#include <string>

namespace fmt {

    // playback clock: "42:13", "1:02:03" (hours only when needed; forceHours keeps "0:42:13")
    std::string clock(double seconds, bool forceHours = false);

    // "00:42:13 / 01:55:20" style pair with the same width on both sides
    std::string clockPair(double position, double duration);

    // "2 h 25 min", "45 min", "1 h" ("" for <= 0)
    std::string duration(double seconds);

    // "42 min left", "1 h 5 min left"
    std::string remaining(double position, double duration);

    // "S01E03"
    std::string episodeCode(int season, int episode);

    // "★ 7.9" ("" for 0); one decimal, ".0" dropped
    std::string rating(float rating);

    // "1080p" / "720p" / "4K" from a frame height ("" when unknown)
    std::string resolution(int width, int height);
}

#endif // PS4IPTV_CORE_FORMAT_H
