// Wall clock in the console's local time zone.

#ifndef PS4IPTV_PLATFORM_CLOCK_H
#define PS4IPTV_PLATFORM_CLOCK_H

#include <cstdint>
#include <string>

namespace clockx {

    // Reads the console time zone (sceSystemServiceParamGetInt TIME_ZONE / SUMMERTIME on PS4).
    void init();

    int64_t unixNow();

    // seconds since an arbitrary start, monotonic enough for UI timing
    double monotonic();

    std::string localTime();         // "21:45"

    std::string localDate(int64_t unixTime);   // "21 Sep 2027"

    // minutes east of UTC currently applied
    int utcOffsetMinutes();
}

#endif // PS4IPTV_PLATFORM_CLOCK_H
