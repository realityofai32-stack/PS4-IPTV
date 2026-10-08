#include <cmath>
#include <cstdio>

#include "format.h"
#include "../i18n/i18n.h"

namespace fmt {

    std::string clock(double seconds, bool forceHours) {
        long s = seconds > 0 ? (long) seconds : 0;
        long h = s / 3600;
        long m = (s / 60) % 60;
        long sec = s % 60;
        char buf[32];
        if (h > 0 || forceHours) {
            snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", h, m, sec);
        } else {
            snprintf(buf, sizeof(buf), "%ld:%02ld", m, sec);
        }
        return buf;
    }

    std::string clockPair(double position, double duration) {
        bool hours = duration >= 3600 || position >= 3600;
        if (duration <= 0) {
            return clock(position, hours);
        }
        return clock(position, hours) + " / " + clock(duration, hours);
    }

    std::string duration(double seconds) {
        long minutes = seconds > 0 ? (long) std::lround(seconds / 60.0) : 0;
        if (minutes <= 0) {
            return seconds > 0 ? i18n::tr("duration.minutes", {"1"}) : "";
        }
        long h = minutes / 60;
        long m = minutes % 60;
        if (h == 0) {
            return i18n::tr("duration.minutes", {std::to_string(m)});
        }
        return m == 0 ? i18n::tr("duration.hours", {std::to_string(h)})
                      : i18n::tr("duration.hours_minutes", {std::to_string(h), std::to_string(m)});
    }

    std::string remaining(double position, double total) {
        double left = total - position;
        if (total <= 0 || left <= 0) {
            return "";
        }
        return i18n::tr("duration.left", {duration(left)});
    }

    std::string episodeCode(int season, int episode) {
        char buf[32];
        snprintf(buf, sizeof(buf), "S%02dE%02d", season < 0 ? 0 : season, episode < 0 ? 0 : episode);
        return buf;
    }

    std::string rating(float r) {
        if (r <= 0) {
            return "";
        }
        char buf[32];
        double rounded = std::round(r * 10.0) / 10.0;
        if (std::fabs(rounded - std::round(rounded)) < 0.05) {
            snprintf(buf, sizeof(buf), "\xE2\x98\x85 %d", (int) std::lround(rounded));
        } else {
            snprintf(buf, sizeof(buf), "\xE2\x98\x85 %.1f", rounded);
        }
        return buf;
    }

    std::string resolution(int width, int height) {
        if (height <= 0) {
            return "";
        }
        // cinema crops (1920x800, 1920x1040) are named by width
        if (width >= 3800 || height >= 2000) {
            return "4K";
        }
        if (width >= 1900 || height >= 1000) {
            return "1080p";
        }
        if (width >= 1260 || height >= 700) {
            return "720p";
        }
        return std::to_string(height) + "p";
    }
}
