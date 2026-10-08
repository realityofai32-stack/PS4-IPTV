// Live TV playback screen on the proven pPlay backend (player/playback + pplay VideoTexture).
//
// Before every open the selected stability preset's mpv options are applied (player/stability.h). A
// stability::Recovery state machine watches playback progress every frame and decides when to show
// "Buffering...", when to reconnect (bounded, with back-off), when to try the other stream format, how
// long to wait after HTTP 403, and when to give up. Circle always leaves (cancelling any pending retry);
// X retries at once.

#include <cmath>

#include "common.h"
#include "../player/display.h"
#include "../iptv/xtream.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../player/pplay/video_texture.h"

using namespace c2d;
using namespace iptv;

namespace {

    const double OVERLAY_TIMEOUT = 5.0;
    const double ZAP_DEBOUNCE = 0.65;
    const float CENTRE_W = 1160;
    const float CENTRE_H = 300;
    const float PILL_W = 460;
    const float PILL_H = 72;

    stability::FormatSetting formatSetting(StreamFormat f) {
        return f == StreamFormat::Ts ? stability::FormatSetting::PreferTs
                                     : f == StreamFormat::Hls ? stability::FormatSetting::PreferHls
                                                              : stability::FormatSetting::Auto;
    }

    // i18n-exempt-begin: English status names for the log
    const char *statusName(stability::Status s) {
        switch (s) {
            case stability::Status::Opening:
                return "opening";
            case stability::Status::Playing:
                return "playing";
            case stability::Status::Buffering:
                return "buffering";
            case stability::Status::Reconnecting:
                return "reconnecting";
            case stability::Status::Refused:
                return "waiting (HTTP 403)";
            default:
                return "failed";
        }
    }
    // i18n-exempt-end

    const char *statusKey(stability::Status s) {
        switch (s) {
            case stability::Status::Opening:
                return "recovery.status.opening";
            case stability::Status::Playing:
                return "recovery.status.playing";
            case stability::Status::Buffering:
                return "recovery.status.buffering";
            case stability::Status::Reconnecting:
                return "recovery.status.reconnecting";
            case stability::Status::Refused:
                return "recovery.status.refused";
            default:
                return "recovery.status.failed";
        }
    }

    const char *stabilityKey(StabilityPreset p) {
        return p == StabilityPreset::Fast ? "stability.fast" : p == StabilityPreset::MaxStability ? "stability.max"
                                                                                                  : "stability.balanced";
    }

    class LivePlayerScreen : public Screen {
    public:
        LivePlayerScreen(App &a, std::vector<int> channelList, int start,
                         std::function<void(const std::string &)> exitCallback)
                : Screen(a), list(std::move(channelList)), index(start), onExit(std::move(exitCallback)) {
            setFillColor(Color::Black);
            app.setPlaybackActive(true);   // downloads pause while a stream is decoded
            video = new VideoTexture(app.playback().backend(), {theme::SCREEN_W, theme::SCREEN_H});
            add(video);

            // top overlay
            top = ui::box(this, FloatRect(0, 0, theme::SCREEN_W, 190), Color(0, 0, 0, 170), 0);
            title = ui::label(top, "", theme::TITLE, theme::SAFE_X, 44, ui::Weight::SemiBold);
            title->setMaxWidth(1300);
            subtitle = ui::label(top, "", theme::LABEL, theme::SAFE_X, 110, ui::Weight::Regular, theme::textDim());
            subtitle->setMaxWidth(1300);
            clock = ui::label(top, "", theme::HEADING, 0, 48, ui::Weight::SemiBold);
            clock->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);
            fav = ui::label(top, "", theme::BODY, 0, 110, ui::Weight::SemiBold, theme::warning());
            fav->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);

            // bottom overlay
            bottom = ui::box(this, FloatRect(0, theme::SCREEN_H - 170, theme::SCREEN_W, 170), Color(0, 0, 0, 170), 0);
            status = ui::label(bottom, "", theme::BODY, theme::SAFE_X, 24, ui::Weight::SemiBold);
            status->setMaxWidth(1700);
            hints = new ui::HintBar();
            hints->setPosition(theme::SAFE_X, 92);
            bottom->add(hints);
            hints->setHints({{ui::Glyph::Cross, tr("live.controls")}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, tr("live.channel")},
                             {ui::Glyph::Square, tr("common.favorite")}, {ui::Glyph::Triangle, tr("player.info")},
                             {ui::Glyph::Circle, tr("common.back")}});

            // centre: opening / reconnecting / HTTP 403 countdown / failure
            centre = ui::box(this, FloatRect((theme::SCREEN_W - CENTRE_W) / 2, 380, CENTRE_W, CENTRE_H),
                             Color(12, 16, 22, 230), theme::RADIUS);
            centreTitle = ui::label(centre, "", theme::HEADING, 0, 52, ui::Weight::SemiBold);
            centreTitle->setAlign(ui::Align::Center, CENTRE_W);
            centreTitle->setMaxWidth(CENTRE_W - 80);
            centreText = ui::label(centre, "", theme::BODY, 0, 118, ui::Weight::Regular, theme::textDim());
            centreText->setAlign(ui::Align::Center, CENTRE_W);
            centreText->setMaxWidth(CENTRE_W - 80);
            centreText->setMaxLines(2);
            centreHint = ui::label(centre, "", theme::LABEL, 0, 222, ui::Weight::Regular, theme::textMuted());
            centreHint->setAlign(ui::Align::Center, CENTRE_W);
            spinner = new ui::Spinner(18);
            spinner->setPosition((CENTRE_W - 18 * 3.6f) / 2, 230);
            centre->add(spinner);

            // small status pill over the picture: "Buffering..."
            pill = ui::box(this, FloatRect((theme::SCREEN_W - PILL_W) / 2, 760, PILL_W, PILL_H),
                           Color(12, 16, 22, 210), PILL_H / 2);
            pillText = ui::label(pill, "", theme::BODY, 84, ui::Label::centerOffset(theme::BODY, PILL_H),
                                 ui::Weight::SemiBold);
            pillText->setMaxWidth(PILL_W - 110);
            pillSpinner = new ui::Spinner(12);
            pillSpinner->setPosition(30, (PILL_H - 12) / 2);
            pill->add(pillSpinner);
            pill->setVisibility(Visibility::Hidden);

            // technical info
            info = ui::box(this, FloatRect(theme::SCREEN_W - theme::SAFE_X - 660, 220, 660, 560),
                           Color(12, 16, 22, 230), theme::RADIUS);
            ui::label(info, tr("info.title"), theme::HEADING, 36, 30, ui::Weight::SemiBold);
            infoText = ui::label(info, "", theme::LABEL, 36, 96, ui::Weight::Regular, theme::textDim());
            infoText->setMaxWidth(600);
            infoText->setMaxLines(15);
            info->setVisibility(app.settings().get().showTechnicalInfo ? Visibility::Visible : Visibility::Hidden);
        }

        ~LivePlayerScreen() override {
            app.playback().stop();
            app.setPlaybackActive(false);
        }

        const char *name() const override { return "live-player"; }

        bool animating() const override { return true; }  // video: draw every frame

        void onEnter() override {
            startChannel(index, true);
        }

        void tick(double now) override {
            Playback &pb = app.playback();
            pb.setFramesRendered(video->getFramesRendered());  // counter is reset on every open()

            // debounced zapping
            if (pendingIndex >= 0 && now >= zapAt) {
                int target = pendingIndex;
                pendingIndex = -1;
                startChannel(target, false);
            }
            if (pendingIndex < 0) {
                supervise(now);
            }
            if (pb.started() && !historyRecorded) {
                historyRecorded = true;
                recordHistory();
            }
            if (overlayVisible && now - lastInput > OVERLAY_TIMEOUT && recovery.status() == stability::Status::Playing
                && pendingIndex < 0) {
                setOverlay(false);
            }
            if (now - lastRefresh >= 0.25) {
                lastRefresh = now;
                refresh(now);
            }
        }

        void handleInput(const InputEvent &e) override {
            lastInput = app.now();
            switch (e.button) {
                case PadButton::Circle:
                    if (e.repeat) {
                        return;
                    }
                    // leaving cancels any pending reconnect / HTTP 403 retry (the destructor stops playback)
                    if (recovery.waitingToReopen()) {
                        LOG_I("player", "retry canceled by the user");
                    }
                    if (onExit) {
                        onExit(channel(pendingIndex >= 0 ? pendingIndex : index).id);
                    }
                    app.pop();
                    return;
                case PadButton::Cross: {
                    if (e.repeat) {
                        return;
                    }
                    stability::Status s = recovery.status();
                    bool stalledNoRetry = s == stability::Status::Buffering && !recovery.reason().empty()
                                          && !app.settings().get().retryOnStall;
                    if (pendingIndex < 0 && (s == stability::Status::Failed || s == stability::Status::Refused
                                             || s == stability::Status::Reconnecting || stalledNoRetry)) {
                        LOG_I("player", "retry requested by the user (%s)", statusName(s));
                        recovery.retryNow(app.now());
                        open();
                    } else {
                        setOverlay(!overlayVisible);
                    }
                    return;
                }
                case PadButton::Up:
                case PadButton::L1:
                    zap(-1);  // previous channel
                    return;
                case PadButton::Down:
                case PadButton::R1:
                    zap(1);   // next channel
                    return;
                case PadButton::Square:
                    if (!e.repeat) {
                        bool on = app.library().toggleFavorite(ContentType::Live, channel(index).id);
                        app.saveLibrary();
                        app.toast(tr(on ? "favorites.added" : "favorites.removed"));
                        setOverlay(true);
                    }
                    return;
                case PadButton::Triangle:
                    if (!e.repeat) {
                        info->setVisibility(info->isVisible() ? Visibility::Hidden : Visibility::Visible);
                        refresh(app.now());
                    }
                    return;
                default:
                    setOverlay(true);
                    return;
            }
        }

    private:
        const LiveChannel &channel(int i) const {
            return app.session().live.channels()[(size_t) list[(size_t) i]];
        }

        std::string categoryName(const std::string &id) const {
            for (const auto &c: app.session().live.categories()) {
                if (c.id == id) {
                    return c.name;
                }
            }
            return "";
        }

        void zap(int delta) {
            int base = pendingIndex >= 0 ? pendingIndex : index;
            int n = (int) list.size();
            if (n <= 1) {
                return;
            }
            pendingIndex = (base + delta + n) % n;
            zapAt = app.now() + ZAP_DEBOUNCE;
            setOverlay(true);
            refresh(app.now());
        }

        void startChannel(int i, bool first) {
            index = i;
            historyRecorded = false;
            const Settings &s = app.settings().get();
            plan = stability::planFormats(formatSetting(s.streamFormat), app.session().account.outputFormats,
                                          app.session().learnedLiveFormat);
            format = plan.first;
            preset = s.stability;
            recovery.begin(stability::policy(preset), s.retryOnStall, !plan.fallback.empty(), app.now());
            LOG_I("player", "%s channel %s (%s): format %s%s%s, stability %s, auto-retry %s", first ? "open" : "zap",
                  channel(index).id.c_str(), channel(index).name.c_str(), format.c_str(),
                  plan.fallback.empty() ? "" : " then ", plan.fallback.c_str(), stabilityName(preset),
                  s.retryOnStall ? "on" : "off");
            open();
            setOverlay(true);
        }

        void open() {
            Playback &pb = app.playback();
            pb.applyOptions(stability::mpvOptions(preset));
            // the default video geometry (mpv keeps vo properties between files: a change made in a movie's
            // Options panel must not carry over to Live TV)
            pb.applyOptions(display::mpvOptions(app.settings().get().defaultGeometry(), (int) theme::SCREEN_W,
                                                (int) theme::SCREEN_H));
            std::string url = xtream::liveUrl(app.session().profile, channel(index).id, format);
            pb.open(url, format == "ts" ? "MPEG-TS" : "HLS");
            video->resetFrameStats();
            refresh(app.now());
        }

        // feeds the recovery state machine and performs what it decides
        void supervise(double now) {
            Playback &pb = app.playback();
            stability::Observation o;
            o.hadFrame = pb.started();
            switch (pb.state()) {
                case PlaybackState::Error:
                    o.phase = stability::Phase::Failed;
                    o.failKind = pb.failKind();
                    break;
                case PlaybackState::Ended:
                    o.phase = stability::Phase::Ended;
                    break;
                case PlaybackState::Buffering:
                case PlaybackState::Playing:
                case PlaybackState::Paused:
                    if (pb.started()) {
                        o.phase = stability::Phase::Running;
                        o.sinceProgress = pb.state() == PlaybackState::Paused ? 0 : pb.sinceProgress(now);
                        o.pausedForCache = pb.pausedForCache();
                        o.dataArriving = pb.info().cacheSpeed > 0;
                        break;
                    }
                    [[fallthrough]];   // loaded, but no picture yet
                default:
                    o.phase = stability::Phase::Opening;
                    o.sinceOpen = pb.openSeconds(now);
                    break;
            }
            stability::Status before = recovery.status();
            stability::Action action = recovery.update(o, now);
            stability::Status after = recovery.status();
            if (after != before) {
                LOG_I("player", "status %s -> %s (attempt %d/%d%s%s)", statusName(before), statusName(after),
                      recovery.attempts(), recovery.maxAttempts(), recovery.reason().empty() ? "" : ", ",
                      recovery.reason().c_str());
            }
            // release the stream while waiting: frees the provider's connection slot before the re-open
            if ((recovery.waitingToReopen() || after == stability::Status::Failed) && pb.state() != PlaybackState::Idle
                && pb.state() != PlaybackState::Error && pb.state() != PlaybackState::Ended) {
                pb.stop();
            }
            if (action == stability::Action::ReopenOtherFormat) {
                format = format == plan.first ? plan.fallback : plan.first;
                LOG_I("player", "no picture with %s: trying %s once", format == "ts" ? "HLS" : "MPEG-TS",
                      format == "ts" ? "MPEG-TS" : "HLS");
                open();
            } else if (action == stability::Action::Reopen) {
                LOG_I("player", "reconnecting (%s, attempt %d/%d)", format.c_str(), recovery.attempts(),
                      recovery.maxAttempts());
                open();
            }
            // Auto mode remembers a format that only worked as the fallback, for the next channels
            if (pb.started() && recovery.fallbackUsed() && format != plan.first
                && app.settings().get().streamFormat == StreamFormat::Auto
                && app.session().learnedLiveFormat != format) {
                app.session().learnedLiveFormat = format;
                LOG_I("player", "auto format: %s works for this provider, using it first from now on", format.c_str());
            }
        }

        void recordHistory() {
            HistoryEntry h;
            h.type = ContentType::Live;
            h.id = channel(index).id;
            h.name = channel(index).name;
            h.icon = channel(index).icon;
            h.watchedAt = clockx::unixNow();
            app.library().addHistory(h);
            app.saveLibrary();
        }

        void setOverlay(bool show) {
            overlayVisible = show;
            top->setVisibility(show ? Visibility::Visible : Visibility::Hidden);
            bottom->setVisibility(show ? Visibility::Visible : Visibility::Hidden);
        }

        void refresh(double now) {
            Playback &pb = app.playback();
            int shown = pendingIndex >= 0 ? pendingIndex : index;
            const LiveChannel &c = channel(shown);
            title->setText((c.num > 0 ? std::to_string(c.num) + "   " : std::string()) + c.name);
            subtitle->setText(categoryName(c.categoryId));
            clock->setText(clockx::localTime());
            fav->setText(app.library().isFavorite(ContentType::Live, c.id) ? "\xE2\x98\x85 " + tr("common.favorite") : "");

            const StreamInfo &si = pb.info();
            stability::Status rs = recovery.status();
            int secondsLeft = (int) std::ceil(recovery.secondsLeft(now));
            std::string attempt = tr("player.attempt_of", {std::to_string(recovery.attempts()),
                                                           std::to_string(recovery.maxAttempts())});
            bool zapping = pendingIndex >= 0;

            std::string st;
            if (zapping) {
                st = tr("live.switching");
            } else if (rs == stability::Status::Playing) {
                st = tr("live.live");
                if (si.height > 0) {
                    st += "   \xC2\xB7   " + std::to_string(si.height) + "p";
                }
                if (!pb.hasAudio()) {
                    st += "   \xC2\xB7   " + tr("live.no_audio");
                }
            } else if (rs == stability::Status::Buffering) {
                st = tr("player.buffering");
            } else if (rs == stability::Status::Reconnecting) {
                st = tr("player.reconnecting");
            } else if (rs == stability::Status::Refused) {
                st = tr("live.retrying_in", {std::to_string(secondsLeft)});
            } else if (rs == stability::Status::Failed) {
                st = tr("live.unavailable");
            } else {
                st = tr("player.opening");
            }
            status->setText(st);
            status->setColor(rs == stability::Status::Failed ? theme::danger() : theme::text());

            // centre panel: opening (no picture yet), reconnecting, HTTP 403 countdown, failure
            bool centreShown = !zapping && (rs == stability::Status::Opening || rs == stability::Status::Reconnecting
                                           || rs == stability::Status::Refused || rs == stability::Status::Failed);
            centre->setVisibility(centreShown ? Visibility::Visible : Visibility::Hidden);
            bool spinning = centreShown && (rs == stability::Status::Opening || rs == stability::Status::Reconnecting);
            spinner->setVisibility(spinning ? Visibility::Visible : Visibility::Hidden);
            centreHint->setVisibility(spinning ? Visibility::Hidden : Visibility::Visible);
            centreTitle->setColor(rs == stability::Status::Failed ? theme::danger() : theme::text());
            if (spinning) {
                spinner->tick(now);
            }
            if (rs == stability::Status::Opening) {
                centreTitle->setText(c.name);
                double secs = pb.openSeconds(now);
                centreText->setText(secs > 15 ? tr("live.slow_server")
                                              : format == "ts" ? tr("player.opening") : tr("live.opening_hls"));
            } else if (rs == stability::Status::Reconnecting) {
                centreTitle->setText(tr("player.reconnecting"));
                centreText->setText(secondsLeft > 0 ? tr("player.next_attempt", {std::to_string(secondsLeft), attempt})
                                                    : tr("player.attempt", {attempt}) + (format == "ts" ? "" : "  (HLS)"));
            } else if (rs == stability::Status::Refused) {
                centreTitle->setText(tr("player.http_403"));
                centreText->setText(tr("player.retrying_in", {std::to_string(secondsLeft), attempt}));
                centreHint->setText(tr("player.hint_retry_now"));
            } else if (rs == stability::Status::Failed) {
                bool stopped = pb.state() == PlaybackState::Error;
                centreTitle->setText(stopped && !pb.errorMessage().empty() ? pb.errorMessage() : tr("live.unavailable"));
                centreText->setText(recovery.attempts() > 0 ? tr("player.gave_up", {std::to_string(recovery.attempts())})
                                                            : pb.state() == PlaybackState::Ended
                                                              ? tr("live.ended") : "");
                centreHint->setText(tr("player.hint_retry"));
            }

            // buffering pill over the (frozen) picture
            bool pillShown = !zapping && rs == stability::Status::Buffering;
            pill->setVisibility(pillShown ? Visibility::Visible : Visibility::Hidden);
            if (pillShown) {
                bool manual = !app.settings().get().retryOnStall && !recovery.reason().empty();
                pillText->setText(manual ? tr("live.buffering_manual") : tr("player.buffering"));
                pillSpinner->tick(now);
            }

            if (info->isVisible()) {
                std::string reason = recovery.reasonText();
                std::vector<std::pair<const char *, std::string>> lines = {
                        {"info.type", si.format},
                        {"info.resolution", si.width > 0 ? std::to_string(si.width) + " x " + std::to_string(si.height) : "-"},
                        {"info.frame_rate", si.fps > 0 ? diag::format("%.2f fps", si.fps) : "-"},
                        {"info.video_codec", si.videoCodec.empty() ? "-" : si.videoCodec},
                        {"info.pixel_format", si.pixelFormat.empty() ? "-" : si.pixelFormat},
                        {"info.audio_codec", si.audioCodec.empty() ? "-" : si.audioCodec},
                        {"info.audio", si.sampleRate > 0 ? diag::format("%d Hz, ", si.sampleRate) + tracks::channelsName(si.channels) : "-"},
                        {"info.output", si.audioOutput.empty() ? "-" : tr("info.output_value", {si.audioOutput})},
                        {"info.buffer", diag::format("%.1f s", si.cacheSeconds)},
                        {"info.network", si.cacheSpeed > 0 ? diag::format("%lld KB/s", si.cacheSpeed / 1000) : "-"},
                        {"info.dropped", std::to_string(si.droppedFrames)},
                        {"info.stability", tr(stabilityKey(preset))},
                        {"info.state", tr(statusKey(rs))},
                        {"info.recovery", std::to_string(recovery.attempts()) + " / " + std::to_string(recovery.maxAttempts())
                                          + (recovery.fallbackUsed() ? ", " + tr("info.fallback_used") : "")
                                          + (reason.empty() ? "" : ", " + tr("info.last", {reason}))}};
                std::string text;
                for (const auto &l: lines) {
                    text += (text.empty() ? "" : "\n") + tr(l.first) + ":  " + l.second;
                }
                infoText->setText(text);
            }
        }

        std::vector<int> list;
        int index;
        std::function<void(const std::string &)> onExit;
        int pendingIndex = -1;
        double zapAt = 0;
        std::string format = "ts";
        stability::FormatPlan plan;
        StabilityPreset preset = StabilityPreset::Balanced;
        stability::Recovery recovery;
        bool historyRecorded = false;
        bool overlayVisible = true;
        double lastInput = 0;
        double lastRefresh = 0;

        VideoTexture *video;
        RectangleShape *top;
        RectangleShape *bottom;
        RectangleShape *centre;
        RectangleShape *pill;
        RectangleShape *info;
        ui::Label *title;
        ui::Label *subtitle;
        ui::Label *clock;
        ui::Label *fav;
        ui::Label *status;
        ui::Label *centreTitle;
        ui::Label *centreText;
        ui::Label *centreHint;
        ui::Label *pillText;
        ui::Label *infoText;
        ui::Spinner *spinner;
        ui::Spinner *pillSpinner;
        ui::HintBar *hints;
    };
}

namespace screens {
    Screen *makeLivePlayer(App &app, const std::vector<int> &channels, int index,
                           std::function<void(const std::string &)> onExit) {
        return new LivePlayerScreen(app, channels, index, std::move(onExit));
    }
}
