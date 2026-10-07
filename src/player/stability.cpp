#include <algorithm>

#include "stability.h"

const char *stabilityName(StabilityPreset p) {
    switch (p) {
        case StabilityPreset::Fast:
            return "Fast";
        case StabilityPreset::MaxStability:
            return "Maximum stability";
        default:
            return "Balanced";
    }
}

namespace stability {

    Options mpvOptions(StabilityPreset p) {
        // mpv options (mpv 0.34.1):
        //   cache                 demux packet cache on for every stream (default "auto" = network streams)
        //   cache-secs            read-ahead target in seconds
        //   demuxer-max-bytes     memory ceiling of the read-ahead
        //   cache-pause           pause while the cache is empty (mpv default, kept explicit)
        //   cache-pause-initial   buffer cache-pause-wait seconds before the first frame
        //   cache-pause-wait      seconds that must be buffered before playback resumes after a stall
        //   network-timeout       seconds without data before FFmpeg reports a network error
        // FFmpeg 5.0 http protocol options via stream-lavf-o (TS stream and HLS playlist requests):
        //   reconnect / reconnect_streamed / reconnect_on_network_error: transparent reconnect of a dropped
        //   live connection (mpv sets reconnect=1 itself but not reconnect_streamed, so live TS never did)
        //   reconnect_delay_max   give up reconnecting after delays 0,1,3,7.. exceed this many seconds
        // FFmpeg 5.0 hls demuxer option via demuxer-lavf-o (ignored by the TS demuxer):
        //   live_start_index      start this many segments behind the live edge (default -3): the older
        //                         segments download in a burst and become the jitter buffer
        const char *seconds;
        const char *maxBytes;
        const char *initial;
        const char *wait;
        const char *timeout;
        const char *delayMax;
        const char *startIndex;
        switch (p) {
            case StabilityPreset::Fast:
                seconds = "10", maxBytes = "32MiB", initial = "no", wait = "1", timeout = "8", delayMax = "2";
                startIndex = "-2";
                break;
            case StabilityPreset::MaxStability:
                seconds = "60", maxBytes = "96MiB", initial = "yes", wait = "8", timeout = "25", delayMax = "8";
                startIndex = "-6";
                break;
            default:
                seconds = "30", maxBytes = "64MiB", initial = "yes", wait = "3", timeout = "15", delayMax = "4";
                startIndex = "-3";
                break;
        }
        return {
                {"cache", "yes"},
                {"cache-secs", seconds},
                {"demuxer-max-bytes", maxBytes},
                {"cache-pause", "yes"},
                {"cache-pause-initial", initial},
                {"cache-pause-wait", wait},
                {"network-timeout", timeout},
                {"stream-lavf-o", std::string("reconnect=1,reconnect_streamed=1,reconnect_on_network_error=1,"
                                              "reconnect_delay_max=") + delayMax},
                {"demuxer-lavf-o", std::string("live_start_index=") + startIndex},
        };
    }

    const std::vector<std::string> &verifiedOptionNames() {
        static const std::vector<std::string> names = {
                "cache", "cache-secs", "demuxer-max-bytes", "cache-pause", "cache-pause-initial",
                "cache-pause-wait", "network-timeout", "stream-lavf-o", "demuxer-lavf-o"};
        return names;
    }

    Policy policy(StabilityPreset p) {
        Policy pol;
        switch (p) {
            case StabilityPreset::Fast:
                pol.stallReconnect = 6;
                pol.stallHardLimit = 12;
                pol.openTimeout = 15;
                pol.maxAttempts = 3;
                pol.reconnectDelays = {0.5, 2, 5};
                pol.refusedDelays = {8, 15, 25};
                break;
            case StabilityPreset::MaxStability:
                pol.stallReconnect = 18;
                pol.stallHardLimit = 35;
                pol.openTimeout = 40;
                pol.maxAttempts = 8;
                pol.reconnectDelays = {2, 4, 8, 12, 20, 30};
                pol.refusedDelays = {10, 20, 30};
                break;
            default:
                pol.stallReconnect = 10;
                pol.stallHardLimit = 20;
                pol.openTimeout = 25;
                pol.maxAttempts = 5;
                pol.reconnectDelays = {1, 3, 6, 10, 15};
                pol.refusedDelays = {10, 20, 30};
                break;
        }
        return pol;
    }

    FormatPlan planFormats(FormatSetting setting, const std::vector<std::string> &allowed,
                           const std::string &learned) {
        bool tsOk = allowed.empty();
        bool hlsOk = allowed.empty();
        for (const auto &f: allowed) {
            tsOk |= f == "ts";
            hlsOk |= f == "m3u8";
        }
        if (!tsOk && !hlsOk) {
            tsOk = hlsOk = true;   // the account lists neither (e.g. only rtmp): try both anyway
        }
        std::string pref = setting == FormatSetting::PreferHls ? "m3u8" : "ts";
        if (setting == FormatSetting::Auto && (learned == "ts" || learned == "m3u8")) {
            pref = learned;
        }
        std::string other = pref == "ts" ? "m3u8" : "ts";
        bool prefOk = pref == "ts" ? tsOk : hlsOk;
        bool otherOk = other == "ts" ? tsOk : hlsOk;
        FormatPlan plan;
        plan.first = prefOk ? pref : other;
        plan.fallback = prefOk && otherOk ? other : "";
        return plan;
    }

    // ------------------------------------------------------------------ Recovery

    double Recovery::delayAt(const std::vector<double> &delays, int index) {
        if (delays.empty()) {
            return 5;
        }
        return delays[(size_t) std::min(std::max(index, 0), (int) delays.size() - 1)];
    }

    void Recovery::begin(const Policy &p, bool retry, bool fallback, double now) {
        (void) now;
        pol = p;
        autoRetry = retry;
        hasFallback = fallback;
        switched = false;
        waiting = false;
        reopenAt = 0;
        playingSince = -1;
        attempts_ = 0;
        refused = 0;
        st = Status::Opening;
        why.clear();
    }

    double Recovery::secondsLeft(double now) const {
        return waiting ? std::max(0.0, reopenAt - now) : 0.0;
    }

    Action Recovery::retryNow(double now) {
        (void) now;
        attempts_ = 0;
        refused = 0;
        waiting = false;
        playingSince = -1;
        st = Status::Opening;
        return Action::Reopen;
    }

    Action Recovery::fail(FailKind kind, bool beforeFirstFrame, const char *reason, double now) {
        why = reason;
        playingSince = -1;
        // the other format once, immediately, when this one never showed a picture (not for HTTP 403: the
        // provider refuses the account, not the format)
        if (beforeFirstFrame && hasFallback && !switched && kind != FailKind::Refused) {
            switched = true;
            st = attempts_ > 0 ? Status::Reconnecting : Status::Opening;
            return Action::ReopenOtherFormat;
        }
        if (kind == FailKind::Fatal || !autoRetry) {
            st = Status::Failed;
            return Action::None;
        }
        if (attempts_ >= pol.maxAttempts) {
            st = Status::Failed;
            why = std::string(reason) + " (gave up after " + std::to_string(attempts_) + " attempts)";
            return Action::None;
        }
        attempts_++;
        waiting = true;
        if (kind == FailKind::Refused) {
            st = Status::Refused;
            reopenAt = now + delayAt(pol.refusedDelays, refused++);
        } else {
            st = Status::Reconnecting;
            reopenAt = now + delayAt(pol.reconnectDelays, attempts_ - 1);
        }
        return Action::None;
    }

    Action Recovery::update(const Observation &o, double now) {
        if (st == Status::Failed) {
            return Action::None;
        }
        if (waiting) {
            if (now < reopenAt) {
                return Action::None;
            }
            waiting = false;
            st = Status::Reconnecting;   // until the re-opened stream shows a picture
            return Action::Reopen;
        }
        switch (o.phase) {
            case Phase::Failed:
                return fail(o.failKind, !o.hadFrame, o.failKind == FailKind::Refused ? "HTTP 403"
                                                     : o.failKind == FailKind::Fatal ? "playback error"
                                                                                     : "connection error", now);
            case Phase::Ended:
                return fail(FailKind::Retryable, !o.hadFrame, "stream ended", now);
            case Phase::Opening:
                if (o.sinceOpen > pol.openTimeout) {
                    return fail(FailKind::Retryable, true, "no picture in time", now);
                }
                return Action::None;
            case Phase::Running:
            default:
                break;
        }
        bool stalled = o.sinceProgress >= pol.bufferingAfter || o.pausedForCache;
        if (!stalled) {
            if (st != Status::Playing) {
                st = Status::Playing;
                playingSince = now;
            }
            if (now - playingSince >= pol.healthyReset) {
                attempts_ = 0;   // healthy again: a later incident gets the full budget
                refused = 0;
            }
            return Action::None;
        }
        st = Status::Buffering;
        playingSince = -1;
        bool give = o.sinceProgress >= pol.stallHardLimit
                    || (o.sinceProgress >= pol.stallReconnect && !o.dataArriving);
        if (give && autoRetry) {
            return fail(FailKind::Retryable, false, "playback stalled", now);
        }
        if (give) {
            why = "playback stalled";   // automatic retry off: keep buffering, X reconnects
        }
        return Action::None;
    }
}
