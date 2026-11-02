// Wall clock in the console's local time zone.

#ifndef PS4IPTV_PLATFORM_CLOCK_H
#define PS4IPTV_PLATFORM_CLOCK_H

#include <cstdint>
#include <string>

namespace clockx {

    // Reads the console time zone (sceSystemServiceParamGetInt TIME_ZONE / SUMMERTIME on PS4).
    void init();

    int64_t unixNow();

    // wall-clock seconds since the app started (key repeat, scrolling, toasts, timeouts). On PS4 this is the
    // FreeBSD kernel's CLOCK_MONOTONIC, never std::chrono::steady_clock (see clock.cpp).
    double monotonic();

    // one log line comparing the UI clock with the console's process time and CPU clock: on hardware the first
    // two advance together, the CPU clock much more slowly
    std::string clockReport();

    std::string localTime();         // "21:45"

    std::string localDate(int64_t unixTime);   // "21 Sep 2027"

    // minutes east of UTC currently applied
    int utcOffsetMinutes();
}

#endif // PS4IPTV_PLATFORM_CLOCK_H
