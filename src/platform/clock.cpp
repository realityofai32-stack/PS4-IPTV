#include <chrono>
#include <cstdio>
#include <ctime>

#ifdef __PS4__
#include <time.h>
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>
#endif

#include "clock.h"
#include "log.h"
#include "../i18n/i18n.h"

namespace clockx {

    namespace {
        int g_offsetMinutes = 0;

#ifdef __PS4__
        // The PS4 kernel is FreeBSD's: CLOCK_MONOTONIC is clock id 4 there. The toolchain's libc++ was built
        // against musl headers that carry the Linux value (1), so std::chrono::steady_clock asks the kernel for
        // clock 1 = FreeBSD's CLOCK_VIRTUAL, the process's user CPU time. That clock hardly moves while the UI
        // idles (the main loop sleeps between frames), which stretched the 0.30 s hold-to-scroll delay to many
        // seconds on the console. UI timing must never come from steady_clock on PS4.
        const clockid_t FREEBSD_CLOCK_MONOTONIC = (clockid_t) 4;
        const clockid_t FREEBSD_CLOCK_VIRTUAL = (clockid_t) 1;

        bool readClock(clockid_t id, double &seconds) {
            struct timespec ts = {};
            if (clock_gettime(id, &ts) != 0) {
                return false;
            }
            seconds = (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
            return true;
        }

        // wall-clock seconds from an arbitrary origin: the kernel's monotonic clock, or the process time
        // (microseconds since the process started) if that clock were ever refused
        double rawSeconds() {
            static const bool kernelMonotonic = [] {
                double probe = 0;
                return readClock(FREEBSD_CLOCK_MONOTONIC, probe);
            }();
            double s = 0;
            if (kernelMonotonic && readClock(FREEBSD_CLOCK_MONOTONIC, s)) {
                return s;
            }
            return (double) sceKernelGetProcessTime() * 1e-6;
        }
#else
        double rawSeconds() {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
#endif

        double g_start = rawSeconds();

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
        return rawSeconds() - g_start;
    }

    std::string clockReport() {
        char buf[160];
#ifdef __PS4__
        double cpu = 0;
        bool hasCpu = readClock(FREEBSD_CLOCK_VIRTUAL, cpu);
        snprintf(buf, sizeof(buf), "ui clock %.2f s, process time %.2f s, cpu clock %s%.2f s", monotonic(),
                 (double) sceKernelGetProcessTime() * 1e-6, hasCpu ? "" : "n/a ", cpu);
#else
        snprintf(buf, sizeof(buf), "ui clock %.2f s (steady_clock)", monotonic());
#endif
        return buf;
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
