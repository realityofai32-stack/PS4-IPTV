// Live TV playback stability presets, stream format planning and the stall/reconnect state machine
// (host-testable: no mpv calls here; the player screen applies the options and feeds observations).
//
// Every mpv option and FFmpeg AVOption below was checked against the exact sources of the PS4 build
// (mpv 0.34.1 + PacBrew PS4 patch, FFmpeg 5.0 - the tarballs pinned by the PacBrew recipes) and found as a
// string in the linked libmpv.a / libavformat.a. See docs/checkpoint-1.5.txt for the per-preset table.
//
// mpv 0.34.1 defaults the presets change: cache=auto, cache-secs=3600000 (effectively unlimited, bounded
// by demuxer-max-bytes=150MiB), cache-pause-initial=no, cache-pause-wait=1 s, network-timeout=60 s, and for
// http streams mpv itself sets the FFmpeg options reconnect=1 / reconnect_delay_max=7 but not
// reconnect_streamed - so a live (non-seekable) TS connection that drops is never reconnected by FFmpeg,
// and a silent connection is only noticed after 60 s.

#ifndef PS4IPTV_PLAYER_STABILITY_H
#define PS4IPTV_PLAYER_STABILITY_H

#include <string>
#include <utility>
#include <vector>

enum class StabilityPreset {
    Fast,
    Balanced,
    MaxStability
};

const char *stabilityName(StabilityPreset p);      // "Fast" / "Balanced" / "Maximum stability"

namespace stability {

    using Options = std::vector<std::pair<std::string, std::string>>;

    // options set on the mpv handle before every Live TV open (all of them, so no value leaks between
    // presets)
    Options mpvOptions(StabilityPreset preset);

    // the option names mpvOptions() may use (verified against the build; tests check against this list)
    const std::vector<std::string> &verifiedOptionNames();

    // app-level recovery policy of a preset
    struct Policy {
        double bufferingAfter = 1.0;      // no playback progress for this long: show "Buffering..."
        double stallReconnect = 10;       // stalled this long and no data arriving: reconnect
        double stallHardLimit = 20;       // stalled this long: reconnect even if data trickles in
        double openTimeout = 25;          // no first frame this long after an open: reconnect
        double healthyReset = 30;         // smooth playback this long: the attempt budget is restored
        int maxAttempts = 5;              // automatic re-opens per incident (reconnects + HTTP 403 retries)
        std::vector<double> reconnectDelays;   // wait before re-open #1, #2, ... (last value repeats)
        std::vector<double> refusedDelays;     // wait after HTTP 403 #1, #2, ...
    };

    Policy policy(StabilityPreset preset);

    // ------------------------------------------------------------------ stream format
    // Live stream format setting values (StreamFormat in settings_store.h, kept independent here)
    enum class FormatSetting {
        Auto,         // TS first (or the format that worked last in this session), other format once
        PreferTs,     // TS first, HLS once if TS cannot be opened
        PreferHls     // HLS first, TS once if HLS cannot be opened
    };

    struct FormatPlan {
        std::string first;      // "ts" or "m3u8"
        std::string fallback;   // "" = none
    };

    // allowedOutputs: the account's allowed_output_formats ("ts", "m3u8", "rtmp"; empty = unknown, all).
    // learned: the format that last played after a fallback in Auto mode ("" = none).
    FormatPlan planFormats(FormatSetting setting, const std::vector<std::string> &allowedOutputs,
                           const std::string &learned);

    // ------------------------------------------------------------------ recovery
    enum class Phase {
        Opening,     // open issued, no frame yet
        Running,     // had a first frame
        Ended,       // the live stream ended (EOF)
        Failed       // the player reported an error
    };

    enum class FailKind {
        Retryable,   // network / server error, stall
        Refused,     // HTTP 403
        Fatal        // retrying cannot help: unsupported codec/format, HTTP 401/404, renderer
    };

    struct Observation {
        Phase phase = Phase::Opening;
        FailKind failKind = FailKind::Retryable;   // Phase::Failed only
        bool hadFrame = false;                     // the current open showed a picture
        double sinceOpen = 0;                      // seconds since the current open
        double sinceProgress = 0;                  // Phase::Running: seconds since playback last advanced
        bool pausedForCache = false;               // mpv paused-for-cache
        bool dataArriving = false;                 // mpv cache-speed > 0
    };

    enum class Status {
        Opening,          // first open of this channel
        Playing,
        Buffering,
        Reconnecting,     // waiting before / performing an automatic re-open
        Refused,          // HTTP 403: waiting for the countdown
        Failed            // gave up (or automatic retry is off): user may press X
    };

    enum class Action {
        None,
        Reopen,               // open the current format again
        ReopenOtherFormat     // switch to the fallback format and open
    };

    class Recovery {

    public:

        // a new channel: resets everything
        void begin(const Policy &policy, bool autoRetry, bool hasFallback, double now);

        // call every frame with the current observation; returns what the player must do now
        Action update(const Observation &o, double now);

        // the user asked to retry (X): immediate re-open with a fresh attempt budget
        Action retryNow(double now);

        Status status() const { return st; }

        int attempts() const { return attempts_; }

        int maxAttempts() const { return pol.maxAttempts; }

        bool fallbackUsed() const { return switched; }

        // a re-open is scheduled (the player should release the stream while it waits)
        bool waitingToReopen() const { return waiting; }

        // seconds until the scheduled re-open (Reconnecting / Refused), 0 when none
        double secondsLeft(double now) const;

        // short reason of the last incident, for the info panel / log
        const std::string &reason() const { return why; }

        // the same, in the UI language ("" when none)
        std::string reasonText() const;

    private:

        Action fail(FailKind kind, bool beforeFirstFrame, const char *reason, double now);

        static double delayAt(const std::vector<double> &delays, int index);

        Policy pol;
        bool autoRetry = true;
        bool hasFallback = false;
        bool switched = false;
        bool waiting = false;          // a re-open is scheduled at reopenAt
        double reopenAt = 0;
        double playingSince = -1;
        int attempts_ = 0;
        int refused = 0;
        Status st = Status::Opening;
        std::string why;
        const char *whyKey = nullptr;   // localization key of why
        int gaveUpAfter = 0;
    };
}

#endif // PS4IPTV_PLAYER_STABILITY_H
