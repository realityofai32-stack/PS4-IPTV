// Live TV stability: preset -> mpv option mapping, stream format planning, stall/reconnect state machine,
// and the new settings.

#include <algorithm>

#include "check.h"
#include "../../src/player/stability.h"
#include "../../src/storage/settings_store.h"

using namespace stability;

namespace {
    std::string opt(const Options &o, const char *name) {
        for (const auto &kv: o) {
            if (kv.first == name) {
                return kv.second;
            }
        }
        return "<missing>";
    }

    Observation running(double sinceProgress, bool dataArriving = false) {
        Observation o;
        o.phase = Phase::Running;
        o.hadFrame = true;
        o.sinceProgress = sinceProgress;
        o.dataArriving = dataArriving;
        return o;
    }

    Observation failed(FailKind kind, bool hadFrame) {
        Observation o;
        o.phase = Phase::Failed;
        o.failKind = kind;
        o.hadFrame = hadFrame;
        return o;
    }

    Observation opening(double sinceOpen) {
        Observation o;
        o.phase = Phase::Opening;
        o.sinceOpen = sinceOpen;
        return o;
    }
}

TEST(stability_presets_map_to_verified_mpv_options) {
    const auto &names = verifiedOptionNames();
    for (StabilityPreset p: {StabilityPreset::Fast, StabilityPreset::Balanced, StabilityPreset::MaxStability}) {
        Options o = mpvOptions(p);
        CHECK_EQ(o.size(), names.size());   // every preset sets every option: nothing leaks between presets
        for (const auto &kv: o) {
            CHECK(std::find(names.begin(), names.end(), kv.first) != names.end());
            CHECK(!kv.second.empty());
        }
        CHECK(opt(o, "cache") == "yes");
        CHECK(opt(o, "stream-lavf-o").find("reconnect_streamed=1") != std::string::npos);
        CHECK(opt(o, "demuxer-lavf-o").find("live_start_index=-") == 0);
    }
    Options fast = mpvOptions(StabilityPreset::Fast);
    Options bal = mpvOptions(StabilityPreset::Balanced);
    Options max = mpvOptions(StabilityPreset::MaxStability);
    CHECK(opt(fast, "cache-pause-initial") == "no" && opt(fast, "cache-pause-wait") == "1");
    CHECK(opt(bal, "cache-pause-initial") == "yes" && opt(bal, "cache-pause-wait") == "3");
    CHECK(opt(max, "cache-pause-initial") == "yes" && opt(max, "cache-pause-wait") == "8");
    CHECK(opt(fast, "network-timeout") == "8" && opt(bal, "network-timeout") == "15"
          && opt(max, "network-timeout") == "25");
    CHECK(opt(fast, "cache-secs") == "10" && opt(bal, "cache-secs") == "30" && opt(max, "cache-secs") == "60");
    CHECK(opt(fast, "demuxer-max-bytes") == "32MiB" && opt(bal, "demuxer-max-bytes") == "64MiB"
          && opt(max, "demuxer-max-bytes") == "96MiB");
    CHECK(opt(bal, "stream-lavf-o") == "reconnect=1,reconnect_streamed=1,reconnect_on_network_error=1,"
                                       "reconnect_delay_max=4");
    CHECK(opt(fast, "demuxer-lavf-o") == "live_start_index=-2");
    CHECK(opt(max, "demuxer-lavf-o") == "live_start_index=-6");
    // tolerance grows with the preset
    Policy pf = policy(StabilityPreset::Fast);
    Policy pb = policy(StabilityPreset::Balanced);
    Policy pm = policy(StabilityPreset::MaxStability);
    CHECK(pf.stallReconnect < pb.stallReconnect && pb.stallReconnect < pm.stallReconnect);
    CHECK(pf.maxAttempts < pb.maxAttempts && pb.maxAttempts < pm.maxAttempts);
    CHECK(pf.openTimeout < pb.openTimeout && pb.openTimeout < pm.openTimeout);
    for (const Policy &p: {pf, pb, pm}) {
        CHECK(p.stallHardLimit > p.stallReconnect && p.stallReconnect > p.bufferingAfter);
        CHECK(!p.reconnectDelays.empty() && !p.refusedDelays.empty());
    }
    CHECK(std::string(stabilityName(StabilityPreset::MaxStability)) == "Maximum stability");
}

TEST(stream_format_plan) {
    FormatPlan p = planFormats(FormatSetting::Auto, {}, "");
    CHECK(p.first == "ts" && p.fallback == "m3u8");
    p = planFormats(FormatSetting::Auto, {"m3u8", "ts", "rtmp"}, "m3u8");     // Auto remembers what worked
    CHECK(p.first == "m3u8" && p.fallback == "ts");
    p = planFormats(FormatSetting::PreferTs, {}, "m3u8");                     // Prefer ignores it
    CHECK(p.first == "ts" && p.fallback == "m3u8");
    p = planFormats(FormatSetting::PreferHls, {}, "");
    CHECK(p.first == "m3u8" && p.fallback == "ts");
    p = planFormats(FormatSetting::PreferTs, {"m3u8"}, "");                   // account allows HLS only
    CHECK(p.first == "m3u8" && p.fallback.empty());
    p = planFormats(FormatSetting::PreferHls, {"ts"}, "");
    CHECK(p.first == "ts" && p.fallback.empty());
    p = planFormats(FormatSetting::Auto, {"rtmp"}, "");                        // neither listed: try both
    CHECK(p.first == "ts" && p.fallback == "m3u8");
}

TEST(recovery_buffering_then_bounded_reconnects) {
    Policy pol = policy(StabilityPreset::Balanced);
    Recovery r;
    r.begin(pol, true, true, 0);
    CHECK(r.status() == Status::Opening);
    CHECK(r.update(opening(2), 2) == Action::None);
    CHECK(r.update(running(0), 3) == Action::None);
    CHECK(r.status() == Status::Playing);
    // short hiccup: Buffering, no reconnect
    CHECK(r.update(running(1.5), 10) == Action::None);
    CHECK(r.status() == Status::Buffering);
    CHECK(r.update(running(0.2), 11) == Action::None);
    CHECK(r.status() == Status::Playing);
    // stall while data still arrives: keep waiting past stallReconnect, until the hard limit
    CHECK(r.update(running(pol.stallReconnect + 1, true), 20) == Action::None);
    CHECK(r.status() == Status::Buffering);
    CHECK_EQ(r.attempts(), 0);
    // nothing arriving any more: reconnect is scheduled (not immediate), the stream must be released
    double t = 30;
    CHECK(r.update(running(pol.stallReconnect + 0.1, false), t) == Action::None);
    CHECK(r.status() == Status::Reconnecting);
    CHECK(r.waitingToReopen());
    CHECK_EQ(r.attempts(), 1);
    CHECK(r.secondsLeft(t) > 0);
    CHECK(r.update(running(99), t + pol.reconnectDelays[0] - 0.01) == Action::None);
    CHECK(r.update(running(99), t + pol.reconnectDelays[0]) == Action::Reopen);
    CHECK(r.status() == Status::Reconnecting);   // until the new open shows a picture
    // every further failure consumes the budget, then it gives up - never an endless loop
    int reopens = 1;
    t += 100;
    for (int i = 0; i < 50; i++) {
        r.update(failed(FailKind::Retryable, true), t);
        t += 100;
        if (r.update(opening(0), t) == Action::Reopen) {
            reopens++;
        }
        if (r.status() == Status::Failed) {
            break;
        }
    }
    CHECK_EQ(reopens, pol.maxAttempts);
    CHECK(r.status() == Status::Failed);
    CHECK(r.reason().find("gave up") != std::string::npos);
    CHECK(r.update(failed(FailKind::Retryable, true), t + 1000) == Action::None);   // stays failed
    // the user can always retry by hand with a fresh budget
    CHECK(r.retryNow(t) == Action::Reopen);
    CHECK_EQ(r.attempts(), 0);
    CHECK(r.status() == Status::Opening);
}

TEST(recovery_hard_limit_even_with_trickling_data) {
    Policy pol = policy(StabilityPreset::Fast);
    Recovery r;
    r.begin(pol, true, false, 0);
    r.update(running(0), 1);
    CHECK(r.update(running(pol.stallHardLimit - 0.5, true), 5) == Action::None);
    CHECK(r.status() == Status::Buffering);
    r.update(running(pol.stallHardLimit, true), 6);
    CHECK(r.status() == Status::Reconnecting);
}

TEST(recovery_http_403_countdown) {
    Policy pol = policy(StabilityPreset::Balanced);
    Recovery r;
    r.begin(pol, true, true, 0);
    // 403 before the first frame: no format fallback (the account is refused, not the format)
    CHECK(r.update(failed(FailKind::Refused, false), 1) == Action::None);
    CHECK(r.status() == Status::Refused);
    CHECK(!r.fallbackUsed());
    CHECK(r.secondsLeft(1) > pol.refusedDelays[0] - 0.01 && r.secondsLeft(1) <= pol.refusedDelays[0]);
    CHECK(r.update(failed(FailKind::Refused, false), 5) == Action::None);   // countdown runs
    CHECK(r.update(opening(0), 1 + pol.refusedDelays[0]) == Action::Reopen);
    // second 403 waits longer
    r.update(failed(FailKind::Refused, false), 20);
    CHECK(r.status() == Status::Refused);
    CHECK(r.secondsLeft(20) > pol.refusedDelays[0]);
    CHECK(r.update(opening(0), 20 + pol.refusedDelays[1]) == Action::Reopen);
    // now it plays: Playing, and after healthyReset the budget is restored
    r.update(running(0), 60);
    CHECK(r.status() == Status::Playing);
    CHECK_EQ(r.attempts(), 2);
    r.update(running(0), 60 + pol.healthyReset + 1);
    CHECK_EQ(r.attempts(), 0);
}

TEST(recovery_format_fallback_once_and_fatal_errors) {
    Policy pol = policy(StabilityPreset::Balanced);
    Recovery r;
    r.begin(pol, true, true, 0);
    // TS never showed a picture: switch to the other format at once, without using an attempt
    CHECK(r.update(failed(FailKind::Retryable, false), 3) == Action::ReopenOtherFormat);
    CHECK(r.fallbackUsed());
    CHECK_EQ(r.attempts(), 0);
    // the fallback fails too: no second switch, normal reconnect path
    CHECK(r.update(failed(FailKind::Retryable, false), 6) == Action::None);
    CHECK(r.status() == Status::Reconnecting);
    // fatal (unsupported codec / 404) after the switch: give up at once
    Recovery f;
    f.begin(pol, true, false, 0);
    CHECK(f.update(failed(FailKind::Fatal, false), 1) == Action::None);
    CHECK(f.status() == Status::Failed);
    // no picture within openTimeout counts as a failure
    Recovery o;
    o.begin(pol, true, false, 0);
    CHECK(o.update(opening(pol.openTimeout - 1), pol.openTimeout - 1) == Action::None);
    o.update(opening(pol.openTimeout + 0.1), pol.openTimeout + 0.1);
    CHECK(o.status() == Status::Reconnecting);
    // the stream ending is reconnected like a drop
    Recovery e;
    e.begin(pol, true, false, 0);
    e.update(running(0), 1);
    Observation ended;
    ended.phase = Phase::Ended;
    ended.hadFrame = true;
    e.update(ended, 2);
    CHECK(e.status() == Status::Reconnecting);
}

TEST(recovery_retry_off) {
    Policy pol = policy(StabilityPreset::Balanced);
    Recovery r;
    r.begin(pol, false, false, 0);
    r.update(running(0), 1);
    // a stall keeps buffering (mpv may still recover by itself) - no automatic reconnect
    CHECK(r.update(running(pol.stallHardLimit + 5), 30) == Action::None);
    CHECK(r.status() == Status::Buffering);
    CHECK(!r.waitingToReopen());
    // errors stop at Failed, including HTTP 403
    r.update(failed(FailKind::Refused, true), 31);
    CHECK(r.status() == Status::Failed);
    CHECK_EQ(r.attempts(), 0);
}

TEST(settings_stability_and_retry_roundtrip) {
    SettingsStore s("unused");
    CHECK(s.get().stability == StabilityPreset::Balanced);   // defaults
    CHECK(s.get().retryOnStall);
    CHECK(s.get().streamFormat == StreamFormat::Auto);
    s.get().stability = StabilityPreset::MaxStability;
    s.get().retryOnStall = false;
    s.get().streamFormat = StreamFormat::Hls;
    std::string text = s.serialize();
    SettingsStore t("unused");
    std::string err;
    CHECK(t.deserialize(text, &err));
    CHECK(t.get().stability == StabilityPreset::MaxStability);
    CHECK(!t.get().retryOnStall);
    CHECK(t.get().streamFormat == StreamFormat::Hls);
    // a Checkpoint 1 settings.json (no new keys) keeps the new defaults
    CHECK(t.deserialize(R"({"version":1,"streamFormat":"ts","loadImages":false})", &err));
    CHECK(t.get().stability == StabilityPreset::Balanced && t.get().retryOnStall);
    CHECK(t.get().streamFormat == StreamFormat::Ts && !t.get().loadImages);
    CHECK(t.deserialize(R"({"playbackStability":"fast"})", &err) && t.get().stability == StabilityPreset::Fast);
    CHECK(t.deserialize(R"({"playbackStability":"bogus"})", &err) && t.get().stability == StabilityPreset::Balanced);
}
