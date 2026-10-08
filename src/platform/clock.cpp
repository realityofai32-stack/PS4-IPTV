#include <chrono>
#include <cstdio>
#include <ctime>

#ifdef __PS4__
#include <orbis/SystemService.h>
#endif

#include "clock.h"
#include "log.h"
#include "../i18n/i18n.h"

namespace clockx {

    namespace {
        int g_offsetMinutes = 0;

        std::tm toTm(int64_t t) {
            time_t tt = (time_t) t;
            std::tm tm{};
#ifdef _WIN32
            gmtime_s(&tm, &tt);
#else
            gmtime_r(&tt, &tm);
#endif
            return tm;
        }
    }

    void init() {
#ifdef __PS4__
        // OpenOrbis orbis/SystemService.h: int32_t sceSystemServiceParamGetInt(int32_t paramId, int32_t *value)
        // ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE = 4 (minutes east of UTC), _SUMMERTIME = 5 (0/1)
        int32_t tz = 0;
        int32_t dst = 0;
        int32_t rc1 = sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE, &tz);
        int32_t rc2 = sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_SUMMERTIME, &dst);
        LOG_I("clock", "system time zone param: rc=0x%08x value=%d, summertime: rc=0x%08x value=%d",
              (unsigned) rc1, tz, (unsigned) rc2, dst);
        if (rc1 == 0 && tz >= -720 && tz <= 840) {
            g_offsetMinutes = tz + (rc2 == 0 && dst == 1 ? 60 : 0);
        } else {
            LOG_W("clock", "time zone unavailable or out of range: showing UTC");
        }
#endif
    }

    int64_t unixNow() {
        return (int64_t) time(nullptr);
    }

    double monotonic() {
        using namespace std::chrono;
        static const auto start = steady_clock::now();
        return duration<double>(steady_clock::now() - start).count();
    }

    int utcOffsetMinutes() {
        return g_offsetMinutes;
    }

    std::string localTime() {
        std::tm tm = toTm(unixNow() + (int64_t) g_offsetMinutes * 60);
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        return buf;
    }

    std::string localDate(int64_t unixTime) {
        static const char *months[] = {"date.month.1", "date.month.2", "date.month.3", "date.month.4",
                                       "date.month.5", "date.month.6", "date.month.7", "date.month.8",
                                       "date.month.9", "date.month.10", "date.month.11", "date.month.12"};
        std::tm tm = toTm(unixTime + (int64_t) g_offsetMinutes * 60);
        // "21 Sep 2027" / "21 Eyl 2027"
        return i18n::tr("date.format", {std::to_string(tm.tm_mday), i18n::tr(months[tm.tm_mon % 12]),
                                        std::to_string(tm.tm_year + 1900)});
    }
}
