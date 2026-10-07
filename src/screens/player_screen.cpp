// Live TV playback screen on the proven pPlay backend (player/playback + pplay VideoTexture).
//
// Before every open the selected stability preset's mpv options are applied (player/stability.h). A
// stability::Recovery state machine watches playback progress every frame and decides when to show
// "Buffering...", when to reconnect (bounded, with back-off), when to try the other stream format, how
// long to wait after HTTP 403, and when to give up. Circle always leaves (cancelling any pending retry);
// X retries at once.

#include <cmath>

#include "common.h"
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

    class LivePlayerScreen : public Screen {
    public:
        LivePlayerScreen(App &a, std::vector<int> channelList, int start,
                         std::function<void(const std::string &)> exitCallback)
                : Screen(a), list(std::move(channelList)), index(start), onExit(std::move(exitCallback)) {
            setFillColor(Color::Black);
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
            hints->setHints({{ui::Glyph::Cross, "Controls"}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, "Channel"},
                             {ui::Glyph::Square, "Favorite"}, {ui::Glyph::Triangle, "Info"},
                             {ui::Glyph::Circle, "Back"}});

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
            ui::label(info, "Stream information", theme::HEADING, 36, 30, ui::Weight::SemiBold);
            infoText = ui::label(info, "", theme::LABEL, 36, 96, ui::Weight::Regular, theme::textDim());
            infoText->setMaxWidth(600);
            infoText->setMaxLines(15);
            info->setVisibility(app.settings().get().showTechnicalInfo ? Visibility::Visible : Visibility::Hidden);
        }

        ~LivePlayerScreen() override {
            app.playback().stop();
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
                        onExit(channel(pendingIndex >= 0 ? pendingIndex : index).streamId);
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
                        bool on = app.library().toggleFavorite(ContentType::Live, channel(index).streamId);
                        app.saveLibrary();
                        app.toast(on ? "Added to favorites" : "Removed from favorites");
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
                  channel(index).streamId.c_str(), channel(index).name.c_str(), format.c_str(),
                  plan.fallback.empty() ? "" : " then ", plan.fallback.c_str(), stabilityName(preset),
                  s.retryOnStall ? "on" : "off");
            open();
            setOverlay(true);
        }

        void open() {
            Playback &pb = app.playback();
            pb.applyOptions(stability::mpvOptions(preset));
            std::string url = xtream::liveUrl(app.session().profile, channel(index).streamId, format);
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
            h.id = channel(index).streamId;
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
            fav->setText(app.library().isFavorite(ContentType::Live, c.streamId) ? "\xE2\x98\x85 Favorite" : "");

            const StreamInfo &si = pb.info();
            stability::Status rs = recovery.status();
            int secondsLeft = (int) std::ceil(recovery.secondsLeft(now));
            std::string attempt = std::to_string(recovery.attempts()) + " of " + std::to_string(recovery.maxAttempts());
            bool zapping = pendingIndex >= 0;

            std::string st;
            if (zapping) {
                st = "Switching channel" "\xE2\x80\xA6";
            } else if (rs == stability::Status::Playing) {
                st = "LIVE";
                if (si.height > 0) {
                    st += "   \xC2\xB7   " + std::to_string(si.height) + "p";
                }
                if (!pb.hasAudio()) {
                    st += "   \xC2\xB7   no audio";
                }
            } else if (rs == stability::Status::Buffering) {
                st = "Buffering" "\xE2\x80\xA6";
            } else if (rs == stability::Status::Reconnecting) {
                st = "Reconnecting" "\xE2\x80\xA6";
            } else if (rs == stability::Status::Refused) {
                st = "Retrying in " + std::to_string(secondsLeft) + " s" "\xE2\x80\xA6";
            } else if (rs == stability::Status::Failed) {
                st = "Stream unavailable";
            } else {
                st = "Opening" "\xE2\x80\xA6";
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
                centreText->setText(secs > 15 ? "The server is slow to respond" "\xE2\x80\xA6" " still trying."
                                              : format == "ts" ? "Opening" "\xE2\x80\xA6"
                                                               : "Opening (HLS)" "\xE2\x80\xA6");
            } else if (rs == stability::Status::Reconnecting) {
                centreTitle->setText("Reconnecting" "\xE2\x80\xA6");
                centreText->setText(secondsLeft > 0 ? "Connection lost. Next attempt in " + std::to_string(secondsLeft)
                                                      + " s  (attempt " + attempt + ")"
                                                    : "Attempt " + attempt + (format == "ts" ? "" : "  (HLS)"));
            } else if (rs == stability::Status::Refused) {
                centreTitle->setText("Provider temporarily refused the stream (HTTP 403)");
                centreText->setText("Retrying in " + std::to_string(secondsLeft) + " s" "\xE2\x80\xA6"
                                    "  (attempt " + attempt + ")");
                centreHint->setText("X  Retry now          Circle  Cancel");
            } else if (rs == stability::Status::Failed) {
                bool stopped = pb.state() == PlaybackState::Error;
                centreTitle->setText(stopped && !pb.errorMessage().empty() ? pb.errorMessage() : "Stream unavailable");
                centreText->setText(recovery.attempts() > 0 ? "Automatic reconnect gave up after "
                                                              + std::to_string(recovery.attempts()) + " attempts."
                                                            : pb.state() == PlaybackState::Ended
                                                              ? "The stream ended." : "");
                centreHint->setText("X  Retry          Circle  Back");
            }

            // buffering pill over the (frozen) picture
            bool pillShown = !zapping && rs == stability::Status::Buffering;
            pill->setVisibility(pillShown ? Visibility::Visible : Visibility::Hidden);
            if (pillShown) {
                bool manual = !app.settings().get().retryOnStall && !recovery.reason().empty();
                pillText->setText(manual ? "Buffering" "\xE2\x80\xA6" "  X reconnects" : "Buffering" "\xE2\x80\xA6");
                pillSpinner->tick(now);
            }

            if (info->isVisible()) {
                char buf[1400];
                snprintf(buf, sizeof(buf),
                         "Type          %s\nResolution    %s\nFrame rate    %s\nVideo codec   %s\nPixel format  %s\n"
                         "Audio codec   %s\nAudio         %s\nOutput        %s\nBuffer        %.1f s\n"
                         "Network       %s\nDropped       %lld\nStability     %s\nState         %s\n"
                         "Recovery      %s",
                         si.format.c_str(),
                         si.width > 0 ? (std::to_string(si.width) + " x " + std::to_string(si.height)).c_str() : "-",
                         si.fps > 0 ? diag::format("%.2f fps", si.fps).c_str() : "-",
                         si.videoCodec.empty() ? "-" : si.videoCodec.c_str(),
                         si.pixelFormat.empty() ? "-" : si.pixelFormat.c_str(),
                         si.audioCodec.empty() ? "-" : si.audioCodec.c_str(),
                         si.sampleRate > 0 ? diag::format("%d Hz, %s", si.sampleRate,
                                                          si.channels == 1 ? "mono" : si.channels == 2 ? "stereo"
                                                          : (std::to_string(si.channels) + " channels").c_str()).c_str()
                                           : "-",
                         si.audioOutput.empty() ? "-" : ("PS4 audio (" + si.audioOutput + ")").c_str(),
                         si.cacheSeconds,
                         si.cacheSpeed > 0 ? diag::format("%lld KB/s", si.cacheSpeed / 1000).c_str() : "-",
                         si.droppedFrames, stabilityName(preset), statusName(rs),
                         (std::to_string(recovery.attempts()) + " / " + std::to_string(recovery.maxAttempts())
                          + (recovery.fallbackUsed() ? ", format fallback used" : "")
                          + (recovery.reason().empty() ? "" : ", last: " + recovery.reason())).c_str());
                infoText->setText(buf);
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
