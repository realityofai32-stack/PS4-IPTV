// Live TV playback screen on the proven pPlay backend (player/playback + pplay VideoTexture).
//
// Before every open the selected stability preset's mpv options are applied (player/stability.h). A
// stability::Recovery state machine watches playback progress every frame and decides when to show
// "Buffering...", when to reconnect (bounded, with back-off), when to try the other stream format, how
// long to wait after HTTP 403, and when to give up. Circle always leaves (cancelling any pending retry);
// X retries at once.
//
// Playlist (M3U) channels play their media URL as-is through the same backend: no Xtream URL, no TS / HLS
// format fallback (the playlist decides the format), the entry's #EXTVLCOPT User-Agent or the source's custom
// one. URLs the FFmpeg build cannot open (rtsp, mms...) fail at once with a localized reason.
//
// Stalker / MAG channels: every open (first open, zap, reconnect) asks the portal for a fresh address
// (create_link, single-use play token) and plays it with the MAG User-Agent; the portal decides the format.
//
// In-player navigation (app/live_nav.h decides, this screen draws; the video goes on behind every panel): Left opens
// the channel list of the current group on the left edge, Left again the group list next to it; Right opens the
// playing channel's guide on the right edge; Circle steps back. A channel picked in the list plays at once, without
// leaving the player. The lists are the session's catalog indices; rows show only logos already in memory (no image
// download competes with the stream). The guide asks the source for one channel's short guide when it is shown
// (iptv/epg.h: never for Xtream channels without an EPG id; a playlist reads its saved XMLTV guide, downloaded once
// when first needed - app/xmltv_service.h; answers kept in the session).

#include <cmath>

#include "common.h"
#include "../app/live_nav.h"
#include "../core/format.h"
#include "../iptv/epg.h"
#include "../player/display.h"
#include "../iptv/m3u.h"
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

    // i18n-exempt-begin: technical stream format names
    // "HLS" / "MPEG-TS" / "MP4" / "RTMP": what the info overlay calls a playlist channel's stream
    std::string playlistFormat(const std::string &url) {
        std::string key = m3u::stableUrlKey(url);
        std::string ext;
        size_t slash = key.rfind('/');
        size_t dot = key.rfind('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
            for (size_t i = dot + 1; i < key.size(); i++) {
                ext += (char) std::toupper((unsigned char) key[i]);
            }
        }
        if (ext == "M3U8") {
            return "HLS";
        }
        if (ext == "TS") {
            return "MPEG-TS";
        }
        if (!ext.empty() && ext.size() <= 5) {
            return ext;
        }
        std::string scheme = m3u::schemeOf(url);
        for (char &c: scheme) {
            c = (char) std::toupper((unsigned char) c);
        }
        return scheme;
    }
    // i18n-exempt-end

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
                         std::function<void(const std::string &)> exitCallback, livenav::Group group)
                : Screen(a), list(std::move(channelList)), index(start), onExit(std::move(exitCallback)),
                  nav(a.session().live, a.library().favorites(ContentType::Live), list, group, list[(size_t) start]),
                  groupRows(this), channelRows(this) {
            setFillColor(theme::videoBackground());
            app.setPlaybackActive(true);   // downloads pause while a stream is decoded
            app.session().livePlayers++;   // a refreshed playlist waits: `list` holds indices into the catalog
            video = new VideoTexture(app.playback().backend(), {theme::SCREEN_W, theme::SCREEN_H});
            add(video);

            // top overlay
            namespace P = layout::player;
            top = ui::box(this, FloatRect(0, 0, theme::SCREEN_W, P::TOP_FADE_H), theme::none(), 0);
            screens::hudShade(top, P::TOP_FADE_H, 100, true);
            title = ui::label(top, "", theme::TITLE, theme::SAFE_X, P::TITLE_Y, ui::Weight::SemiBold);
            title->setMaxWidth(1300);
            subtitle = ui::label(top, "", theme::LABEL, theme::SAFE_X, P::SUBTITLE_Y + 4, ui::Weight::Regular,
                                 theme::textSecondary());
            subtitle->setMaxWidth(1300);
            clock = ui::label(top, "", theme::HEADING, 0, P::TITLE_Y + 4, ui::Weight::SemiBold);
            clock->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);
            fav = ui::label(top, "", theme::BODY, 0, P::SUBTITLE_Y, ui::Weight::SemiBold, theme::warning());
            fav->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);

            // bottom overlay: status line and hints on a shade that fades up into the picture
            const float bh = 300;
            bottom = ui::box(this, FloatRect(0, theme::SCREEN_H - bh, theme::SCREEN_W, bh), theme::none(), 0);
            screens::hudShade(bottom, bh, 150, false);
            status = ui::label(bottom, "", theme::BODY, theme::SAFE_X, layout::HINTS_Y - (theme::SCREEN_H - bh) - 60,
                               ui::Weight::SemiBold);
            status->setMaxWidth(1700);
            hints = new ui::HintBar();
            hints->setOwner(this);   // its actions are this screen's keyboard shortcuts (also while the HUD is hidden)
            hints->setPosition(theme::SAFE_X, layout::HINTS_Y - (theme::SCREEN_H - bh));
            bottom->add(hints);
            hints->setHints({{A::Confirm, tr("live.controls")}, {A::ChannelList, tr("live.channels")},
                             {A::Guide, tr("live.guide")}, {A::Tabs, tr("live.channel")},
                             {A::Favorite, tr("common.favorite")}, {A::Info, tr("player.info")},
                             {A::Back, tr("common.back")}});

            // centre: opening / reconnecting / HTTP 403 countdown / failure
            centre = ui::box(this, FloatRect((theme::SCREEN_W - CENTRE_W) / 2, 380, CENTRE_W, CENTRE_H),
                             theme::overlayPanel(), theme::RADIUS);
            centreTitle = ui::label(centre, "", theme::HEADING, 0, 52, ui::Weight::SemiBold);
            centreTitle->setAlign(ui::Align::Center, CENTRE_W);
            centreTitle->setMaxWidth(CENTRE_W - 80);
            centreText = ui::label(centre, "", theme::BODY, 0, 118, ui::Weight::Regular, theme::textSecondary());
            centreText->setAlign(ui::Align::Center, CENTRE_W);
            centreText->setMaxWidth(CENTRE_W - 80);
            centreText->setMaxLines(2);
            centreHint = new ui::HintBar();   // [X] Retry  [O] Back - for the device in use
            centreHint->setOwner(this);
            centreHint->setPosition(0, 206);
            centreHint->setSize(CENTRE_W, layout::HINTS_H);
            centre->add(centreHint);
            spinner = new ui::Spinner(18);
            spinner->setPosition((CENTRE_W - 18 * 3.6f) / 2, 230);
            centre->add(spinner);

            // small status pill over the picture: "Buffering..."
            pill = ui::box(this, FloatRect((theme::SCREEN_W - PILL_W) / 2, 760, PILL_W, PILL_H),
                           theme::overlayPanel(), PILL_H / 2);
            pillText = ui::label(pill, "", theme::BODY, 84, ui::Label::centerOffset(theme::BODY, PILL_H),
                                 ui::Weight::SemiBold);
            pillText->setMaxWidth(PILL_W - 110);
            pillSpinner = new ui::Spinner(12);
            pillSpinner->setPosition(30, (PILL_H - 12) / 2);
            pill->add(pillSpinner);
            pill->setVisibility(Visibility::Hidden);

            // technical info
            info = ui::box(this, FloatRect(theme::SCREEN_W - theme::SAFE_X - 660, 220, 660, 560),
                           theme::overlayPanel(), theme::RADIUS);
            ui::label(info, tr("info.title"), theme::HEADING, 36, 30, ui::Weight::SemiBold);
            infoText = ui::label(info, "", theme::LABEL, 36, 96, ui::Weight::Regular, theme::textSecondary());
            infoText->setMaxWidth(600);
            infoText->setMaxLines(15);
            infoWanted = app.settings().get().showTechnicalInfo;
            info->setVisibility(infoWanted ? Visibility::Visible : Visibility::Hidden);

            // the in-player channel list / group list and guide (above everything else, hidden until asked for)
            buildDrawer();
            buildGuide();
        }

        ~LivePlayerScreen() override {
            linkToken.cancel();
            app.playback().stop();
            app.playback().setUserAgent("");
            app.setPlaybackActive(false);
            app.session().livePlayers--;
        }

        const char *name() const override { return "live-player"; }

        bool animating() const override { return true; }  // video: draw every frame

        void onEnter() override {
            startChannel(index, true);
        }

        void tick(double now) override {
            if (nav.tick(now)) {
                syncPanels();   // a panel left alone closed by itself
            }
            if (nav.panel() == livenav::Panel::Guide && (guideDirty || now - lastGuide >= 1.0)) {
                renderGuide(now);
            }
            Playback &pb = app.playback();
            pb.setFramesRendered(video->getFramesRendered());
            pb.setRenderTiming(video->renderTiming());  // counter is reset on every open()

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
            // the channel list / group list / guide first (app/live_nav.h); what they do not take is the player's
            livenav::Outcome o = nav.press(e.button, e.repeat, lastInput);
            if (o.handled) {
                if (o.play) {
                    LOG_I("player", "channel picked in the channel list (%d in the group)", (int) o.list.size());
                    list = std::move(o.list);   // the group becomes the zap list
                    pendingIndex = -1;
                    startChannel(o.index, false);
                }
                if (o.changed) {
                    syncPanels();
                    if (nav.panel() == livenav::Panel::None) {
                        setOverlay(true);   // back to the picture: the channel line for a moment
                    }
                }
                return;
            }
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
                        nav.refresh();   // the Favorites group of the channel list
                        app.toast(tr(on ? "favorites.added" : "favorites.removed"));
                        setOverlay(true);
                    }
                    return;
                case PadButton::Triangle:
                    if (!e.repeat) {
                        infoWanted = !infoWanted;
                        info->setVisibility(infoWanted ? Visibility::Visible : Visibility::Hidden);
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

        void zap(int delta) {
            int base = pendingIndex >= 0 ? pendingIndex : index;
            int n = (int) list.size();
            if (n <= 1) {
                return;
            }
            pendingIndex = (base + delta + n) % n;
            zapAt = app.now() + ZAP_DEBOUNCE;
            setOverlay(true);
            guideDirty = true;   // an open guide names the channel at once (its guide is asked once the zap settles)
            refresh(app.now());
        }

        void startChannel(int i, bool first) {
            index = i;
            historyRecorded = false;
            const Settings &s = app.settings().get();
            if (channel(index).isPlaylist()) {
                // the playlist's URL is the stream: no alternative format to fall back to
                plan = stability::FormatPlan();
                plan.first = playlistFormat(channel(index).url) == "HLS" ? "m3u8" : "ts";
            } else if (app.session().profile.isStalker()) {
                plan = stability::FormatPlan();   // the portal's link decides the format
                plan.first = "ts";
            } else {
                plan = stability::planFormats(formatSetting(s.streamFormat), app.session().account.outputFormats,
                                              app.session().learnedLiveFormat);
            }
            format = plan.first;
            preset = s.stability;
            recovery.begin(stability::policy(preset), s.retryOnStall, !plan.fallback.empty(), app.now());
            LOG_I("player", "%s channel %s (%s): format %s%s%s, stability %s, auto-retry %s", first ? "open" : "zap",
                  channel(index).id.c_str(), channel(index).name.c_str(), format.c_str(),
                  plan.fallback.empty() ? "" : " then ", plan.fallback.c_str(), stabilityName(preset),
                  s.retryOnStall ? "on" : "off");
            open();
            setOverlay(true);
            nav.setPlaying(list[(size_t) index]);   // the lists mark it, the guide follows it
            guideDirty = true;
        }

        void open() {
            Playback &pb = app.playback();
            pb.applyOptions(stability::mpvOptions(preset));
            // the default video geometry (mpv keeps vo properties between files: a change made in a movie's
            // Options panel must not carry over to Live TV)
            pb.applyOptions(display::mpvOptions(app.settings().get().defaultGeometry(), (int) theme::SCREEN_W,
                                                (int) theme::SCREEN_H));
            const LiveChannel &c = channel(index);
            linkToken.cancel();
            if (c.isPlaylist()) {
                pb.setUserAgent(c.userAgent.empty() ? app.session().profile.userAgent : c.userAgent);
                pb.open(c.url, playlistFormat(c.url));
            } else if (app.session().profile.isStalker()) {
                // a fresh single-use address from the portal for every open
                pb.prepare("");
                std::string id = c.id;
                linkToken = app.stalker().resolveLive(app.session().profile, c, [this, id](const sources::LinkOutcome &o) {
                    if (channel(index).id != id) {
                        return;   // zapped meanwhile
                    }
                    Playback &p = app.playback();
                    if (!o.ok) {
                        p.failBeforeOpen(o.message, "create_link failed", true);   // i18n-exempt: technical value
                    } else {
                        p.setUserAgent(o.userAgent);
                        p.open(o.url, playlistFormat(o.url));
                    }
                    refresh(app.now());
                });
            } else {
                pb.setUserAgent("");
                std::string url = xtream::liveUrl(app.session().profile, c.id, format);
                pb.open(url, format == "ts" ? "MPEG-TS" : "HLS");
            }
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
            show = show && nav.panel() == livenav::Panel::None;   // a panel open: no channel line under it
            overlayVisible = show;
            top->setVisibility(show ? Visibility::Visible : Visibility::Hidden);
            bottom->setVisibility(show ? Visibility::Visible : Visibility::Hidden);
        }

        void refresh(double now) {
            Playback &pb = app.playback();
            int shown = pendingIndex >= 0 ? pendingIndex : index;
            const LiveChannel &c = channel(shown);
            title->setText((c.num > 0 ? std::to_string(c.num) + "   " : std::string()) + c.name);
            subtitle->setText(app.session().live.categoryName(c.categoryId));
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
            status->setColor(rs == stability::Status::Failed ? theme::danger() : theme::textPrimary());

            // centre panel: opening (no picture yet), reconnecting, HTTP 403 countdown, failure
            bool centreShown = !zapping && (rs == stability::Status::Opening || rs == stability::Status::Reconnecting
                                           || rs == stability::Status::Refused || rs == stability::Status::Failed);
            centre->setVisibility(centreShown ? Visibility::Visible : Visibility::Hidden);
            bool spinning = centreShown && (rs == stability::Status::Opening || rs == stability::Status::Reconnecting);
            spinner->setVisibility(spinning ? Visibility::Visible : Visibility::Hidden);
            centreHint->setVisibility(spinning ? Visibility::Hidden : Visibility::Visible);
            centreTitle->setColor(rs == stability::Status::Failed ? theme::danger() : theme::textPrimary());
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
                setCentreHints({{A::Confirm, tr("player.retry_now")}, {A::Back, tr("common.cancel")}});
            } else if (rs == stability::Status::Failed) {
                bool stopped = pb.state() == PlaybackState::Error;
                centreTitle->setText(stopped && !pb.errorMessage().empty() ? pb.errorMessage() : tr("live.unavailable"));
                centreText->setText(recovery.attempts() > 0 ? tr("player.gave_up", {std::to_string(recovery.attempts())})
                                                            : pb.state() == PlaybackState::Ended
                                                              ? tr("live.ended") : "");
                setCentreHints({{A::Confirm, tr("player.retry")}, {A::Back, tr("common.back")}});
            }

            // buffering pill over the (frozen) picture
            bool pillShown = !zapping && rs == stability::Status::Buffering;
            pill->setVisibility(pillShown ? Visibility::Visible : Visibility::Hidden);
            if (pillShown) {
                bool manual = !app.settings().get().retryOnStall && !recovery.reason().empty();
                pillText->setText(manual ? tr("live.buffering_manual") : tr("player.buffering"));
                pillSpinner->tick(now);
            }

            if (guideSpinner->isVisible()) {
                guideSpinner->tick(now);
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

        // ------------------------------------------------------------------ in-player channel list / groups
        // rows of the group list: the name and the number of channels
        struct GroupRows : ui::ListView::Adapter {
            explicit GroupRows(LivePlayerScreen *s) : screen(s) {}

            int count() override { return (int) screen->nav.groups().size(); }

            C2DObject *createRow(float w, float h) override {
                Row r;
                r.bg = new ui::RowFrame(w, h, -1);
                r.name = ui::label(r.bg, "", theme::LABEL, layout::ROW_PAD, ui::Label::centerOffset(theme::LABEL, h));
                r.count = ui::label(r.bg, "", theme::CAPTION, 0, ui::Label::centerOffset(theme::CAPTION, h),
                                    ui::Weight::Regular, theme::textMuted());
                r.count->setAlign(ui::Align::Right, w - 14);
                rows.push_back(r);
                return r.bg;
            }

            void bindRow(C2DObject *obj, int i, bool selected, bool focused) override {
                for (auto &r: rows) {
                    if (r.bg != obj) {
                        continue;
                    }
                    r.count->setText(std::to_string(screen->nav.count(i)));
                    r.name->setMaxWidth(r.bg->getSize().x - layout::ROW_PAD - r.count->width() - 26);
                    r.name->setText(screen->nav.name(i));
                    r.name->setWeight(selected ? ui::Weight::SemiBold : ui::Weight::Regular);
                    r.name->setColor(focused ? theme::textStrong() : selected ? theme::textPrimary()
                                                                              : theme::textSecondary());
                    r.count->setColor(focused ? theme::textSecondary() : theme::textMuted());
                    r.bg->setState(focused, selected);
                }
            }

            struct Row {
                ui::RowFrame *bg;
                ui::Label *name;
                ui::Label *count;
            };
            LivePlayerScreen *screen;
            std::vector<Row> rows;
        };

        // rows of the channel list: number, logo (only one already in memory: no download while streaming), name,
        // the playing mark and the favorite star
        struct ChannelRows : ui::ListView::Adapter {
            explicit ChannelRows(LivePlayerScreen *s) : screen(s) {}

            int count() override { return (int) screen->nav.channels().size(); }

            C2DObject *createRow(float w, float h) override {
                namespace O = layout::liveov;
                Row r;
                r.bg = new ui::RowFrame(w, h, O::LOGO_X);
                r.num = ui::label(r.bg, "", theme::CAPTION, 0, ui::Label::centerOffset(theme::CAPTION, h),
                                  ui::Weight::Regular, theme::textMuted());
                r.num->setAlign(ui::Align::Right, O::NUM_W);
                r.logo = new ui::LogoView(FloatRect(O::LOGO_X, (h - O::LOGO_H) / 2, O::LOGO_W, O::LOGO_H), 20, 4, 1.25f);
                r.logo->setCornersRadius(theme::RADIUS_SMALL);
                r.bg->add(r.logo);
                r.name = ui::label(r.bg, "", theme::BODY, O::NAME_X, ui::Label::centerOffset(theme::BODY, h));
                r.name->setMaxWidth(w - O::NAME_X - O::MARK_W);
                r.mark = ui::box(r.bg, FloatRect(w - O::MARK_W + 6, (h - 12) / 2, 12, 12), theme::accent(), 6);
                r.fav = ui::label(r.bg, "", theme::BODY, 0, ui::Label::centerOffset(theme::BODY, h),
                                  ui::Weight::SemiBold, theme::warning());
                r.fav->setAlign(ui::Align::Right, w - 14);
                rows.push_back(r);
                return r.bg;
            }

            void bindRow(C2DObject *obj, int i, bool selected, bool focused) override {
                const LiveCatalog &live = screen->app.session().live;
                int ci = screen->nav.channels()[(size_t) i];
                const LiveChannel &c = live.channels()[(size_t) ci];
                for (auto &r: rows) {
                    if (r.bg != obj) {
                        continue;
                    }
                    std::shared_ptr<ImageSet> img = screen->app.images().get(c.icon);   // memory only: no request
                    r.logo->set(c.name, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                    r.num->setText(c.num > 0 ? std::to_string(c.num) : "");
                    r.name->setText(c.name);
                    bool playing = ci == screen->nav.playing();
                    r.name->setWeight(focused || playing ? ui::Weight::SemiBold : ui::Weight::Regular);
                    r.name->setColor(focused ? theme::textStrong() : playing ? theme::accentText() : theme::textPrimary());
                    r.num->setColor(focused ? theme::textPrimary() : theme::textMuted());
                    r.mark->setVisibility(playing ? Visibility::Visible : Visibility::Hidden);
                    r.fav->setText(screen->app.library().isFavorite(ContentType::Live, c.id) ? "\xE2\x98\x85" : "");
                    r.bg->setState(focused, selected);
                }
            }

            struct Row {
                ui::RowFrame *bg;
                ui::Label *num;
                ui::LogoView *logo;
                ui::Label *name;
                RectangleShape *mark;
                ui::Label *fav;
            };
            LivePlayerScreen *screen;
            std::vector<Row> rows;
        };

        // the left panel: the group list (only while it is open) and the channel list
        void buildDrawer() {
            namespace O = layout::liveov;
            drawer = ui::box(this, FloatRect(0, 0, O::DRAWER_W, theme::SCREEN_H), theme::panel(), 0);
            // a soft right edge instead of a cut into the picture
            drawerFade = ui::fade(drawer, FloatRect(O::DRAWER_W, 0, O::EDGE_W, theme::SCREEN_H), theme::panel(),
                                  ui::Edge::Left);
            groupsTitle = ui::label(drawer, tr("live.groups"), theme::HEADING, O::GROUPS_X, O::TITLE_Y,
                                    ui::Weight::SemiBold);
            groupsTitle->setMaxWidth(O::GROUPS_W);
            groupList = new ui::ListView(FloatRect(O::GROUPS_X, O::LIST_Y, O::GROUPS_W, O::LIST_H), O::ROW_H, O::ROW_GAP,
                                         &groupRows);
            drawer->add(groupList);
            channelsTitle = ui::label(drawer, "", theme::HEADING, O::CH_X, O::TITLE_Y, ui::Weight::SemiBold);
            channelsTitle->setMaxWidth(O::CH_W);
            channelsCount = ui::label(drawer, "", theme::LABEL, O::CH_X, O::CONTEXT_Y, ui::Weight::Regular,
                                      theme::textMuted());
            channelList = new ui::ListView(FloatRect(O::CH_X, O::LIST_Y, O::CH_W, O::LIST_H), O::ROW_H, O::ROW_GAP,
                                           &channelRows);
            drawer->add(channelList);
            channelsEmpty = ui::label(drawer, "", theme::BODY, O::CH_X, O::LIST_Y + 40, ui::Weight::Regular,
                                      theme::textMuted());
            channelsEmpty->setMaxWidth(O::CH_W);
            drawerHints = new ui::HintBar();
            drawerHints->setOwner(this);
            drawer->add(drawerHints);
            drawer->setVisibility(Visibility::Hidden);
            nav.setPageSize(channelList->pageSize());
        }

        // shows the panels as the navigation has them
        void syncPanels() {
            namespace O = layout::liveov;
            livenav::Panel p = nav.panel();
            bool lists = p == livenav::Panel::Channels || p == livenav::Panel::Groups;
            bool groups = p == livenav::Panel::Groups;
            if (p != livenav::Panel::None) {
                setOverlay(false);   // the channel line would sit under the panel
            }
            drawer->setVisibility(lists ? Visibility::Visible : Visibility::Hidden);
            if (lists) {
                float w = groups ? O::DRAWER_W_WITH_GROUPS : O::DRAWER_W;
                drawer->setSize(w, theme::SCREEN_H);
                ui::refade(drawerFade, FloatRect(w, 0, O::EDGE_W, theme::SCREEN_H), theme::panel(), ui::Edge::Left);
                groupsTitle->setVisibility(groups ? Visibility::Visible : Visibility::Hidden);
                groupList->setVisibility(groups ? Visibility::Visible : Visibility::Hidden);
                float cx = groups ? O::CH_X_WITH_GROUPS : O::CH_X;
                channelsTitle->setPosition(cx, O::TITLE_Y);
                channelsCount->setPosition(cx, O::CONTEXT_Y);
                channelList->setPosition(cx, O::LIST_Y);
                channelsEmpty->setPosition(cx, O::LIST_Y + 40);
                groupList->setFocused(groups);
                channelList->setFocused(!groups);
                if (groups) {
                    if (nav.groupSelected() != groupList->selected() && drawnGroups) {
                        groupList->moveSelection(nav.groupSelected() - groupList->selected());
                    } else {
                        groupList->setSelected(nav.groupSelected());
                    }
                }
                drawnGroups = groups;
                // the same group: the selection glides; another group: the list is bound again
                int shownGroup = nav.groupShown();
                if (shownGroup == drawnGroup && (int) nav.channels().size() == drawnCount) {
                    if (nav.channelSelected() != channelList->selected()) {
                        channelList->moveSelection(nav.channelSelected() - channelList->selected());
                    } else {
                        channelList->reload();   // the playing mark / stars
                    }
                } else {
                    channelList->reload();
                    channelList->setSelected(nav.channelSelected());
                }
                drawnGroup = shownGroup;
                drawnCount = (int) nav.channels().size();
                channelsTitle->setText(nav.name(shownGroup));
                channelsCount->setText(nav.channels().empty() ? "" : fmt::number((long long) nav.channels().size()));
                channelsEmpty->setText(nav.channels().empty() ? tr("live.no_channels") : "");
                // size first: the hint bar picks its compact layout from its width when the hints change
                drawerHints->setPosition(O::GROUPS_X, layout::HINTS_Y);
                drawerHints->setSize(w - O::GROUPS_X - O::PAD, layout::HINTS_H);
                if (groups) {
                    drawerHints->setHints({{A::Confirm, tr("common.open")}, {A::Back, tr("common.back")}});
                } else {
                    drawerHints->setHints({{A::Confirm, tr("live.watch")}, {A::Tabs, tr("live.group")},
                                           {A::ChannelList, tr("live.groups")}, {A::Back, tr("common.close")}});
                }
            } else {
                drawnGroup = -1;   // bound again when it opens
                drawnGroups = false;
            }
            bool guideShown = p == livenav::Panel::Guide;
            guidePanel->setVisibility(guideShown ? Visibility::Visible : Visibility::Hidden);
            // the technical info would sit under the guide: hidden meanwhile, back after
            info->setVisibility(infoWanted && !guideShown ? Visibility::Visible : Visibility::Hidden);
            if (guideShown) {
                renderGuide(app.now());
            }
        }

        // ------------------------------------------------------------------ in-player guide
        void buildGuide() {
            namespace O = layout::liveov;
            const float x = O::GUIDE_PAD;
            const float w = O::GUIDE_TEXT_W;
            guidePanel = ui::box(this, FloatRect(O::GUIDE_X, 0, O::GUIDE_W, theme::SCREEN_H), theme::panel(), 0);
            ui::fade(guidePanel, FloatRect(-O::EDGE_W, 0, O::EDGE_W, theme::SCREEN_H), theme::panel(), ui::Edge::Right);
            guideName = ui::label(guidePanel, "", theme::HEADING, x, O::TITLE_Y, ui::Weight::SemiBold);
            guideName->setMaxWidth(w);
            guideMeta = ui::label(guidePanel, "", theme::LABEL, x, O::CONTEXT_Y, ui::Weight::Regular,
                                  theme::textSecondary());
            guideMeta->setMaxWidth(w);
            guideState = ui::label(guidePanel, "", theme::BODY, x, O::LIST_Y + 20, ui::Weight::Regular,
                                   theme::textMuted());
            guideState->setMaxWidth(w);
            guideState->setMaxLines(2);
            guideSpinner = new ui::Spinner(12);
            guideSpinner->setPosition(x, O::LIST_Y + 34);
            guidePanel->add(guideSpinner);
            nowTag = ui::label(guidePanel, tr("live.now"), theme::CAPTION, x, 0, ui::Weight::SemiBold, theme::accentText());
            nowTime = ui::label(guidePanel, "", theme::LABEL, x, 0, ui::Weight::Regular, theme::textSecondary());
            nowTime->setMaxWidth(w);
            nowTitle = ui::label(guidePanel, "", theme::HEADING, x, 0, ui::Weight::SemiBold);
            nowTitle->setMaxWidth(w);
            nowTitle->setMaxLines(2);
            nowTrack = ui::box(guidePanel, FloatRect(x, 0, w, O::GUIDE_BAR_H), theme::progressTrack(), O::GUIDE_BAR_H / 2);
            nowFill = ui::box(nowTrack, FloatRect(0, 0, O::GUIDE_BAR_H, O::GUIDE_BAR_H), theme::accent(),
                              O::GUIDE_BAR_H / 2);
            nowDesc = ui::label(guidePanel, "", theme::LABEL, x, 0, ui::Weight::Regular, theme::textSecondary());
            nowDesc->setMaxWidth(w);
            nowDesc->setMaxLines(7);
            nextTag = ui::label(guidePanel, tr("live.next"), theme::CAPTION, x, 0, ui::Weight::SemiBold,
                                theme::accentText());
            for (auto &l: nextLines) {
                l = ui::label(guidePanel, "", theme::BODY, x, 0, ui::Weight::Regular);
                l->setMaxWidth(w);
            }
            auto *gh = new ui::HintBar();
            gh->setOwner(this);
            gh->setPosition(x, layout::HINTS_Y);
            gh->setSize(w, layout::HINTS_H);
            gh->setHints({{A::ChannelList, tr("live.channels")}, {A::Back, tr("common.close")}});
            guidePanel->add(gh);
            guidePanel->setVisibility(Visibility::Hidden);
        }

        // where the guide of a channel comes from (None: "no program information" without a request)
        epg::Source guideSource(const LiveChannel &c) const {
            return epg::sourceFor(app.session().profile.type, c, !app.session().live.guideUrls.empty());
        }

        // asks the source for the playing channel's guide when it has one and the answer it has is due
        void requestGuide() {
            const LiveChannel &c = channel(index);
            epg::Source source = guideSource(c);
            if (source == epg::Source::None) {
                return;   // no request: the source has no guide for this channel
            }
            epg::Cache &cache = app.session().guide;
            int64_t now = clockx::unixNow();
            if (!cache.due(c.id, now)) {
                return;
            }
            bool offline = app.session().offline;
            if (offline && source != epg::Source::Xmltv) {
                cache.failed(c.id, now);   // the provider is out of reach: not "loading" forever
                return;
            }
            cache.loading(c.id, now);
            App *a = &app;
            unsigned serial = app.session().serial;
            std::string id = c.id;
            std::weak_ptr<bool> alive = guideAlive;
            app.sources().loadGuide(app.session().profile, app.session().live, c, offline,
                                    [a, serial, id, alive, this](sources::GuideOutcome &o) {
                if (a->session().serial != serial) {
                    return;   // signed in to another source meanwhile
                }
                int64_t t = clockx::unixNow();
                if (o.ok) {
                    a->session().guide.store(id, std::move(o.programmes), t);
                } else {
                    a->session().guide.failed(id, t);
                }
                if (alive.lock()) {
                    guideDirty = true;
                }
            });
        }

        // the guide of the channel shown (the zap target while zapping): the programme on air with its time,
        // progress and description, then the next ones - or one clean line when there is nothing to show
        void renderGuide(double t) {
            namespace O = layout::liveov;
            guideDirty = false;
            lastGuide = t;
            int shownIndex = pendingIndex >= 0 ? pendingIndex : index;
            const LiveChannel &c = channel(shownIndex);
            guideName->setText(c.name);
            std::string group = app.session().live.categoryName(c.categoryId);
            guideMeta->setText(c.num > 0 ? std::to_string(c.num) + (group.empty() ? "" : "   \xC2\xB7   " + group) : group);
            if (pendingIndex < 0) {
                requestGuide();   // never for every channel passed while zapping
            }
            for (C2DObject *o: std::initializer_list<C2DObject *>{nowTag, nowTime, nowTitle, nowTrack, nowDesc, nextTag,
                                                                  nextLines[0], nextLines[1], nextLines[2]}) {
                o->setVisibility(Visibility::Hidden);
            }
            bool none = guideSource(c) == epg::Source::None;
            const epg::Cache::Entry &e = none ? noGuide : app.session().guide.get(c.id);
            int64_t now = clockx::unixNow();
            std::string state;
            bool loading = false;
            epg::NowNext nn;
            if (none || e.state == epg::Cache::State::Empty) {
                state = tr("live.no_program");
            } else if (e.state == epg::Cache::State::Failed) {
                state = tr("live.program_failed");
            } else if (e.state == epg::Cache::State::Missing || e.state == epg::Cache::State::Loading) {
                state = tr("live.program_loading");
                loading = true;
            } else {
                nn = epg::nowNext(e.programmes, now);
                if (nn.now < 0 && nn.next < 0) {
                    state = tr("live.no_program");   // everything it sent has ended
                }
            }
            guideState->setText(state);
            guideState->setPosition(loading ? O::GUIDE_PAD + 60 : O::GUIDE_PAD, O::LIST_Y + 20);
            guideSpinner->setVisibility(loading ? Visibility::Visible : Visibility::Hidden);
            if (!state.empty()) {
                return;
            }
            // i18n-exempt-begin: times and separators
            float y = O::LIST_Y + 20;
            if (nn.now >= 0) {
                const epg::Programme &p = e.programmes[(size_t) nn.now];
                nowTag->setPosition(O::GUIDE_PAD, y);
                y += 36;
                nowTime->setText(clockx::localTimeOf(p.start) + " \xE2\x80\x93 " + clockx::localTimeOf(p.stop)
                                 + "   \xC2\xB7   " + tr("live.minutes_left", {std::to_string(epg::minutesLeft(p, now))}));
                nowTime->setPosition(O::GUIDE_PAD, y);
                y += 40;
                nowTitle->setText(p.title);
                nowTitle->setPosition(O::GUIDE_PAD, y);
                y += nowTitle->height() + 18;
                nowTrack->setPosition(O::GUIDE_PAD, y);
                nowFill->setSize(std::max(O::GUIDE_BAR_H, (float) (O::GUIDE_TEXT_W * epg::progress(p, now))),
                                 O::GUIDE_BAR_H);
                y += O::GUIDE_BAR_H + 26;
                nowDesc->setText(p.description);
                nowDesc->setPosition(O::GUIDE_PAD, y);
                if (!p.description.empty()) {
                    y += nowDesc->height() + 36;
                }
                for (C2DObject *o: std::initializer_list<C2DObject *>{nowTag, nowTime, nowTitle, nowTrack}) {
                    o->setVisibility(Visibility::Visible);
                }
                nowDesc->setVisibility(p.description.empty() ? Visibility::Hidden : Visibility::Visible);
            }
            if (nn.next >= 0) {
                nextTag->setPosition(O::GUIDE_PAD, y);
                nextTag->setVisibility(Visibility::Visible);
                y += 36;
                for (int k = 0; k < 3 && nn.next + k < (int) e.programmes.size(); k++) {
                    const epg::Programme &p = e.programmes[(size_t) (nn.next + k)];
                    ui::Label *l = nextLines[k];
                    l->setText(clockx::localTimeOf(p.start) + "   " + p.title);
                    l->setColor(k == 0 ? theme::textPrimary() : theme::textSecondary());
                    l->setPosition(O::GUIDE_PAD, y);
                    l->setVisibility(Visibility::Visible);
                    y += 46;
                }
            }
            // i18n-exempt-end
        }

        std::vector<int> list;
        int index;
        std::function<void(const std::string &)> onExit;
        livenav::Navigator nav;   // the channel list / group list / guide (after `list`: built from it)
        GroupRows groupRows;
        ChannelRows channelRows;
        RectangleShape *drawer = nullptr;
        RectangleShape *drawerFade = nullptr;
        ui::Label *groupsTitle = nullptr;
        ui::ListView *groupList = nullptr;
        ui::Label *channelsTitle = nullptr;
        ui::Label *channelsCount = nullptr;
        ui::ListView *channelList = nullptr;
        ui::Label *channelsEmpty = nullptr;
        ui::HintBar *drawerHints = nullptr;
        int drawnGroup = -1;
        int drawnCount = -1;
        bool drawnGroups = false;
        RectangleShape *guidePanel = nullptr;
        ui::Label *guideName = nullptr;
        ui::Label *guideMeta = nullptr;
        ui::Label *guideState = nullptr;
        ui::Spinner *guideSpinner = nullptr;
        ui::Label *nowTag = nullptr;
        ui::Label *nowTime = nullptr;
        ui::Label *nowTitle = nullptr;
        RectangleShape *nowTrack = nullptr;
        RectangleShape *nowFill = nullptr;
        ui::Label *nowDesc = nullptr;
        ui::Label *nextTag = nullptr;
        ui::Label *nextLines[3] = {nullptr, nullptr, nullptr};
        epg::Cache::Entry noGuide;          // what a channel without a guide source shows
        bool guideDirty = false;
        double lastGuide = 0;
        std::shared_ptr<bool> guideAlive = std::make_shared<bool>(true);   // guide answers after the screen left
        bool infoWanted = false;
        int pendingIndex = -1;
        double zapAt = 0;
        std::string format = "ts";
        CancelToken linkToken;   // Stalker: the create_link request of the current open
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
        ui::HintBar *centreHint;

        void setCentreHints(const screens::Hints &h) {
            centreHint->setHints(h);
            centreHint->setPosition(std::round((CENTRE_W - std::min(CENTRE_W, centreHint->contentWidth())) / 2),
                                    centreHint->getPosition().y);
        }

        void onInputDeviceChanged() override {
            centreHint->setPosition(std::round((CENTRE_W - std::min(CENTRE_W, centreHint->contentWidth())) / 2),
                                    centreHint->getPosition().y);
        }
        ui::Label *pillText;
        ui::Label *infoText;
        ui::Spinner *spinner;
        ui::Spinner *pillSpinner;
        ui::HintBar *hints;
    };
}

namespace screens {
    Screen *makeLivePlayer(App &app, const std::vector<int> &channels, int index,
                           std::function<void(const std::string &)> onExit, livenav::Group group) {
        return new LivePlayerScreen(app, channels, index, std::move(onExit), group);
    }
}
