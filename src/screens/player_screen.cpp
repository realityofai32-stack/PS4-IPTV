// Live TV playback screen on the proven pPlay backend (player/playback + pplay VideoTexture).

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
    const double RETRY_403_DELAY = 20.0;   // the provider in testing released a slot after 30-60 s
    const int RETRY_403_MAX = 3;

    class LivePlayerScreen : public Screen {
    public:
        LivePlayerScreen(App &a, std::vector<int> channelList, int start)
                : Screen(a), list(std::move(channelList)), index(start) {
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

            // centre: opening / error
            centre = ui::box(this, FloatRect((theme::SCREEN_W - 1100) / 2, 380, 1100, 300), Color(12, 16, 22, 230),
                             theme::RADIUS);
            centreTitle = ui::label(centre, "", theme::HEADING, 0, 60, ui::Weight::SemiBold);
            centreTitle->setAlign(ui::Align::Center, 1100);
            centreTitle->setMaxWidth(1000);
            centreText = ui::label(centre, "", theme::BODY, 0, 130, ui::Weight::Regular, theme::textDim());
            centreText->setAlign(ui::Align::Center, 1100);
            centreText->setMaxWidth(1000);
            centreText->setMaxLines(3);
            spinner = new ui::Spinner(18);
            spinner->setPosition((1100 - 18 * 3.6f) / 2, 230);
            centre->add(spinner);

            // technical info
            info = ui::box(this, FloatRect(theme::SCREEN_W - theme::SAFE_X - 620, 230, 620, 480),
                           Color(12, 16, 22, 230), theme::RADIUS);
            ui::label(info, "Stream information", theme::HEADING, 36, 30, ui::Weight::SemiBold);
            infoText = ui::label(info, "", theme::LABEL, 36, 96, ui::Weight::Regular, theme::textDim());
            infoText->setMaxWidth(560);
            infoText->setMaxLines(12);
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
            // automatic, spaced retry after HTTP 403
            if (retryAt > 0 && now >= retryAt) {
                retryAt = 0;
                retries403++;
                LOG_I("player", "HTTP 403 retry %d/%d", retries403, RETRY_403_MAX);
                open();
            }
            // TS failed before the first frame: try HLS once (not for 403/https)
            if (pb.failedBeforeFirstFrame() && !triedFallback && format == "ts"
                && app.settings().get().streamFormat == StreamFormat::Auto && pb.error() != PlaybackError::Http403
                && pb.error() != PlaybackError::HttpsUnsupported) {
                triedFallback = true;
                LOG_I("player", "MPEG-TS failed (%s): trying HLS", pb.errorDetail().c_str());
                format = "m3u8";
                open();
            }
            if (pb.state() == PlaybackState::Error && pb.error() == PlaybackError::Http403 && retryAt <= 0
                && retries403 < RETRY_403_MAX && !retryScheduled) {
                retryScheduled = true;
                retryAt = now + RETRY_403_DELAY;
            }
            if (pb.hasVideoFrame() && !historyRecorded) {
                historyRecorded = true;
                recordHistory();
            }
            if (overlayVisible && now - lastInput > OVERLAY_TIMEOUT && pb.state() == PlaybackState::Playing
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
            Playback &pb = app.playback();
            switch (e.button) {
                case PadButton::Circle:
                    app.pop();
                    return;
                case PadButton::Cross:
                    if (e.repeat) {
                        return;
                    }
                    if (pb.state() == PlaybackState::Error || pb.state() == PlaybackState::Ended) {
                        retries403 = 0;
                        retryAt = 0;
                        open();
                    } else {
                        setOverlay(!overlayVisible);
                    }
                    return;
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
                        bool on = app.library().toggleFavorite(ContentType::Live, channel().streamId);
                        app.saveLibrary();
                        app.toast(on ? "Added to favorites" : "Removed from favorites");
                        setOverlay(true);
                    }
                    return;
                case PadButton::Triangle:
                    if (!e.repeat) {
                        info->setVisibility(info->isVisible() ? Visibility::Hidden : Visibility::Visible);
                    }
                    return;
                default:
                    setOverlay(true);
                    return;
            }
        }

    private:
        const LiveChannel &channel() const {
            return app.session().live.channels()[(size_t) list[(size_t) index]];
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
            triedFallback = false;
            retries403 = 0;
            retryAt = 0;
            historyRecorded = false;
            const Settings &s = app.settings().get();
            const AccountInfo &acc = app.session().account;
            bool tsAllowed = acc.outputFormats.empty();
            bool hlsAllowed = acc.outputFormats.empty();
            for (const auto &f: acc.outputFormats) {
                tsAllowed |= f == "ts";
                hlsAllowed |= f == "m3u8";
            }
            format = s.streamFormat == StreamFormat::Hls || (!tsAllowed && hlsAllowed) ? "m3u8" : "ts";
            LOG_I("player", "%s channel %s (%s), format %s", first ? "open" : "zap", channel().streamId.c_str(),
                  channel().name.c_str(), format.c_str());
            open();
            setOverlay(true);
        }

        void open() {
            retryScheduled = false;
            std::string url = xtream::liveUrl(app.session().profile, channel().streamId, format);
            app.playback().open(url, format == "ts" ? "MPEG-TS" : "HLS");
            video->resetFrameStats();
            refresh(app.now());
        }

        void recordHistory() {
            HistoryEntry h;
            h.type = ContentType::Live;
            h.id = channel().streamId;
            h.name = channel().name;
            h.icon = channel().icon;
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
            const LiveChannel &c = app.session().live.channels()[(size_t) list[(size_t) shown]];
            title->setText((c.num > 0 ? std::to_string(c.num) + "   " : std::string()) + c.name);
            subtitle->setText(categoryName(c.categoryId));
            clock->setText(clockx::localTime());
            fav->setText(app.library().isFavorite(ContentType::Live, c.streamId) ? "\xE2\x98\x85 Favorite" : "");

            const StreamInfo &si = pb.info();
            std::string st;
            if (pendingIndex >= 0) {
                st = "Switching channel" "\xE2\x80\xA6";
            } else if (pb.state() == PlaybackState::Playing) {
                st = "LIVE";
                if (si.height > 0) {
                    st += "   \xC2\xB7   " + std::to_string(si.height) + "p";
                }
                if (!pb.hasAudio()) {
                    st += "   \xC2\xB7   no audio";
                }
            } else {
                st = Playback::stateText(pb.state());
            }
            status->setText(st);
            status->setColor(pb.state() == PlaybackState::Error ? theme::danger() : theme::text());

            // centre panel: opening/buffering/errors
            bool opening = pb.state() == PlaybackState::Opening || (pb.state() == PlaybackState::Buffering
                                                                    && !pb.hasVideoFrame());
            bool error = pb.state() == PlaybackState::Error;
            bool ended = pb.state() == PlaybackState::Ended;
            centre->setVisibility(opening || error || ended ? Visibility::Visible : Visibility::Hidden);
            spinner->setVisibility(opening ? Visibility::Visible : Visibility::Hidden);
            if (opening) {
                spinner->tick(now);
                centreTitle->setText(c.name);
                double secs = pb.openSeconds(now);
                centreText->setText(secs > 20 ? "The server is slow to respond" "\xE2\x80\xA6" " still trying."
                                              : format == "ts" ? "Opening" "\xE2\x80\xA6" : "Opening (HLS)" "\xE2\x80\xA6");
            } else if (error) {
                centreTitle->setText(pb.errorMessage());
                std::string t;
                if (pb.error() == PlaybackError::Http403 && retryAt > 0) {
                    t = "Retrying automatically in " + std::to_string((int) (retryAt - now) + 1)
                        + " s. Press X to retry now, or Circle to go back.";
                } else if (pb.error() == PlaybackError::Http403) {
                    t = "Wait a moment and press X to retry. The provider may still count your previous stream.";
                } else {
                    t = "Press X to retry, or Circle to go back.";
                }
                centreText->setText(t);
            } else if (ended) {
                centreTitle->setText("The stream ended");
                centreText->setText("Press X to reconnect, or Circle to go back.");
            }

            if (info->isVisible()) {
                char buf[1024];
                snprintf(buf, sizeof(buf),
                         "Type          %s\nResolution    %s\nFrame rate    %s\nVideo codec   %s\nPixel format  %s\n"
                         "Audio codec   %s\nAudio         %s\nOutput        %s\nBuffer        %.1f s\nDropped       %lld",
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
                         si.cacheSeconds, si.droppedFrames);
                infoText->setText(buf);
            }
        }

        std::vector<int> list;
        int index;
        int pendingIndex = -1;
        double zapAt = 0;
        std::string format = "ts";
        bool triedFallback = false;
        int retries403 = 0;
        double retryAt = 0;
        bool retryScheduled = false;
        bool historyRecorded = false;
        bool overlayVisible = true;
        double lastInput = 0;
        double lastRefresh = 0;

        VideoTexture *video;
        RectangleShape *top;
        RectangleShape *bottom;
        RectangleShape *centre;
        RectangleShape *info;
        ui::Label *title;
        ui::Label *subtitle;
        ui::Label *clock;
        ui::Label *fav;
        ui::Label *status;
        ui::Label *centreTitle;
        ui::Label *centreText;
        ui::Label *infoText;
        ui::Spinner *spinner;
        ui::HintBar *hints;
    };
}

namespace screens {
    Screen *makeLivePlayer(App &app, const std::vector<int> &channels, int index) {
        return new LivePlayerScreen(app, channels, index);
    }
}
