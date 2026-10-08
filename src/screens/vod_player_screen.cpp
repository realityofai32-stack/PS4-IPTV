// Movie / episode playback on the proven pPlay backend (player/playback + pplay VideoTexture).
//
// Same mpv instance, options and recovery logic as Live TV, plus what VOD needs:
//   - resume: mpv's per-file "start" option at exactly the saved position (precise seek, see Playback)
//   - seeking: Left/Right 10 s, L2/R2 1 min; presses accumulate and one keyframe seek is sent when they stop
//   - tracks: picked automatically when the file loads (Settings: audio language, subtitle mode/language),
//     switched at runtime from the Options panel through mpv's aid / sid (no reload)
//   - progress (app/vod_progress): the store every 5 s, history.json every 15 s; on pause, Circle, next
//     episode and app exit the position is queried from mpv right before stopping and written at once
//   - overlay: hides 4 s after the last input while playing (player/hud_logic.h)
//   - episodes: L1/R1 previous/next, an "Up next" panel at the end (auto-play only when enabled in Settings)
//   - video geometry (player/display): Aspect Ratio, Crop / Fill, Zoom and Position in the Options panel's Video
//     menu, mpv properties set at runtime (no reload: position, pause state and tracks stay). The Settings
//     defaults apply when the player opens; a change in the panel lasts for this playback only, unless the
//     user chooses "Set as default" there
//   - offline: a downloaded item (VodItem::localPath) opens the local file through the same Playback /
//     mpv path; tracks, video geometry, resume and progress work identically. Progress is kept in the download's
//     profile (the library switches to it while the player is open)
//   - downloads pause while the player is open (App::setPlaybackActive): decoding comes first

#include <cctype>
#include <cmath>

#include "common.h"
#include "../core/format.h"
#include "../iptv/xtream.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../app/vod_progress.h"
#include "../player/display.h"
#include "../player/hud_logic.h"
#include "../player/pplay/video_texture.h"

using namespace c2d;
using namespace iptv;
using screens::VodItem;

namespace {

    const double SEEK_COMMIT_DELAY = 0.45;    // seconds after the last seek press
    const double END_MARGIN = 30;             // an end of file this close to the duration is the real end
    const double AUTOPLAY_COUNTDOWN = 10;
    const float PANEL_W = 680;

    const char *SIZE_KEYS[] = {"subtitle.size.small", "subtitle.size.medium", "subtitle.size.large"};
    const char *POSITION_KEYS[] = {"subtitle.position.bottom", "subtitle.position.raised"};

    // the log's English status names
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

    std::string episodeTitle(const VodItem &it) {
        return it.title.empty() ? tr("episode.number", {std::to_string(it.episode)}) : it.title;
    }

    class VodPlayerScreen : public Screen, public ui::ListView::Adapter {
    public:
        VodPlayerScreen(App &a, std::vector<VodItem> items, int start, bool resumeFirst,
                        std::function<void(const std::string &)> exitCallback)
                : Screen(a), queue(std::move(items)), index(start), resumeOnStart(resumeFirst),
                  onExit(std::move(exitCallback)), tracker(a.library(), &clockx::unixNow) {
            setFillColor(Color::Black);
            geometry = app.settings().get().defaultGeometry();
            app.setPlaybackActive(true);
            video = new VideoTexture(app.playback().backend(), {theme::SCREEN_W, theme::SCREEN_H});
            add(video);
            buildOverlay();
            buildCentre();
            buildPanel();
            buildInfo();
        }

        ~VodPlayerScreen() override {
            app.playback().stop();
            app.setPlaybackActive(false);
            if (!previousProfile.empty()) {
                app.library().setProfile(previousProfile);   // back to the signed-in profile
            }
        }

        const char *name() const override { return "vod-player"; }

        bool animating() const override { return true; }

        void onEnter() override {
            startItem(index, resumeOnStart);
        }

        // the app is closing with this player open: keep the exact position
        void onAppExit() override {
            if (!finished) {
                saveFinal("app exit");
            }
        }

        // ------------------------------------------------------------------ frame
        void tick(double now) override {
            Playback &pb = app.playback();
            pb.setFramesRendered(video->getFramesRendered());
            if (!finished) {
                supervise(now);
            }
            if (pendingSeek && now >= seekCommitAt) {
                pendingSeek = false;
                LOG_I("player", "seek to %.1fs", seekTarget);
                pb.seekTo(seekTarget);
                tracker.seekCommitted(seekTarget, now);
                lastActivity = now;
            }
            bool sampling = pb.state() == PlaybackState::Playing || pb.state() == PlaybackState::Paused
                            || pb.state() == PlaybackState::Buffering;
            tracker.observe(sampling ? pb.position() : -1, pb.duration(), pb.started() && sampling, now);
            if (!finished) {
                VodProgress::Due due = tracker.due(now);
                if (due != VodProgress::Due::None) {
                    saveProgress(due == VodProgress::Due::Disk);
                }
            }
            if (!languagesRecorded && !pb.trackList().empty()) {
                languagesRecorded = true;   // offered in Settings > preferred languages
                for (const auto &t: pb.trackList()) {
                    std::string lang = tracks::effectiveLanguage(t);
                    if (!lang.empty()) {
                        app.session().seenLanguages.insert(lang);
                    }
                }
            }
            if (pb.subtitlesDisabledByRenderer() && !subtitleWarningShown) {
                subtitleWarningShown = true;
                app.toast(tr("player.subtitles_unsupported"), ToastKind::Error);
            }
            if (finished && autoplayAt > 0 && now >= autoplayAt) {
                autoplayAt = 0;
                playNext();
                return;
            }
            bool paused = pb.state() == PlaybackState::Paused;
            bool playing = recovery.status() == stability::Status::Playing && !paused;
            if (playing && !wasPlaying) {
                lastActivity = now;   // playback (re)started: the overlay stays for the full 4 s
            }
            wasPlaying = playing;
            hud::State h;
            h.visible = overlayVisible;
            h.playing = playing;
            h.paused = paused;
            h.panelOpen = panelOpen;
            h.seekPending = pendingSeek;
            h.finished = finished;
            h.lastActivity = lastActivity;
            if (hud::shouldAutoHide(h, now)) {
                setOverlay(false);
            }
            if (now - lastRefresh >= 0.25) {
                lastRefresh = now;
                refresh(now);
            }
        }

        // ------------------------------------------------------------------ input
        void handleInput(const InputEvent &e) override {
            lastActivity = app.now();
            if (panelOpen) {
                panelInput(e);
                return;
            }
            Playback &pb = app.playback();
            if (finished) {
                endInput(e);
                return;
            }
            switch (e.button) {
                case PadButton::Circle:
                    if (!e.repeat) {
                        exit();
                    }
                    return;
                case PadButton::Cross: {
                    if (e.repeat) {
                        return;
                    }
                    stability::Status s = recovery.status();
                    if (s == stability::Status::Failed || s == stability::Status::Refused
                        || s == stability::Status::Reconnecting) {
                        LOG_I("player", "retry requested by the user (%s)", statusName(s));
                        recovery.retryNow(app.now());
                        open(tracker.position());
                    } else if (pb.state() == PlaybackState::Playing || pb.state() == PlaybackState::Paused) {
                        bool pause = pb.state() == PlaybackState::Playing;
                        if (pause) {
                            saveFinal("pause");   // the exact paused position
                        }
                        pb.setPaused(pause);
                        setOverlay(true);
                    } else {
                        setOverlay(!overlayVisible);
                    }
                    return;
                }
                case PadButton::Left:
                case PadButton::Right:
                    seekBy(e.button == PadButton::Left ? -10 : 10);
                    return;
                case PadButton::L2:
                case PadButton::R2:
                    seekBy(e.button == PadButton::L2 ? -60 : 60);
                    return;
                case PadButton::L1:
                case PadButton::R1:
                    if (!e.repeat && isEpisode()) {
                        int target = index + (e.button == PadButton::L1 ? -1 : 1);
                        if (target >= 0 && target < (int) queue.size()) {
                            saveFinal("episode change");
                            startItem(target, true);
                        }
                    }
                    return;
                case PadButton::Options:
                    if (!e.repeat) {
                        openPanel();
                    }
                    return;
                case PadButton::Triangle:
                    if (!e.repeat) {
                        info->setVisibility(info->isVisible() ? Visibility::Hidden : Visibility::Visible);
                        refresh(app.now());
                    }
                    return;
                case PadButton::Square:
                    if (!e.repeat) {
                        const VodItem &it = item();
                        ContentType t = isEpisode() ? ContentType::Series : ContentType::Movie;
                        const std::string &id = isEpisode() ? it.seriesId : it.id;
                        bool on = app.library().toggleFavorite(t, id);
                        app.saveLibrary();
                        app.toast(tr(on ? "favorites.added" : "favorites.removed"));
                        setOverlay(true);
                    }
                    return;
                default:
                    setOverlay(true);
                    return;
            }
        }

    private:
        const VodItem &item() const { return queue[(size_t) index]; }

        bool isEpisode() const { return item().type == ContentType::Series; }

        // ------------------------------------------------------------------ playback
        void startItem(int i, bool resume) {
            index = i;
            // a download of another (or a deleted) profile keeps its progress in that profile
            const std::string &owner = queue[(size_t) i].profileId;
            if (!owner.empty() && owner != app.library().profileId()) {
                if (previousProfile.empty()) {
                    previousProfile = app.library().profileId();
                }
                app.library().setProfile(owner);
            }
            finished = false;
            autoplayAt = 0;
            endPanel->setVisibility(Visibility::Hidden);
            pendingSeek = false;
            subtitleWarningShown = false;
            languagesRecorded = false;
            manualAudio = -1;
            manualSub = -2;
            const Settings &s = app.settings().get();
            preset = s.stability;
            recovery.begin(stability::policy(preset), s.retryOnStall, false, app.now());
            // exactly the saved position (no seconds subtracted), when resuming an item in progress
            double start = tracker.begin(item(), resume && s.resumeVod, app.now());
            LOG_I("player", "%s %s (%s, %s), %s, stability %s, aspect %s, crop %s, zoom %d%%",
                  isEpisode() ? "episode" : "movie", item().id.c_str(), item().extension.c_str(),
                  item().localPath.empty() ? "stream" : "downloaded file",
                  start > 0 ? diag::format("resume at %.3fs", start).c_str() : "from the start", stabilityName(preset),
                  display::aspectKey(geometry.aspect), display::cropKey(geometry.crop), geometry.zoom);
            lastActivity = app.now();
            open(start);
            setOverlay(true);
            refresh(app.now());
        }

        void open(double start) {
            Playback &pb = app.playback();
            const Settings &s = app.settings().get();
            pb.applyOptions(stability::mpvOptions(preset));
            pb.applyOptions(tracks::appearanceOptions(s.subtitleSize, s.subtitlePosition, s.subtitleShadow));
            // precise absolute seeks for the resume start (mpv 0.34.1 default, set explicitly against configs)
            pb.applyOptions({{"hr-seek", "default"}});
            pb.applyOptions(display::mpvOptions(geometry, (int) theme::SCREEN_W, (int) theme::SCREEN_H));
            const VodItem &it = item();
            // a downloaded copy plays from the disk (same backend, no provider request)
            std::string url = !it.localPath.empty() ? it.localPath
                              : isEpisode() ? xtream::seriesUrl(app.session().profile, it.id, it.extension)
                                            : xtream::movieUrl(app.session().profile, it.id, it.extension);
            Playback::OpenOptions o;
            o.start = start;
            o.chooseTracks = [this](const std::vector<tracks::Track> &list) { return chooseTracks(list); };
            std::string container = it.extension.empty() ? "VOD" : it.extension;
            for (auto &c: container) {
                c = (char) toupper((unsigned char) c);
            }
            pb.open(url, container, o);
            video->resetFrameStats();
        }

        // automatic choice from Settings; a choice made in the Options panel wins for this playback
        std::pair<int, int> chooseTracks(const std::vector<tracks::Track> &list) {
            const Settings &s = app.settings().get();
            int aid = -1;
            if (manualAudio > 0) {
                aid = manualAudio;
            } else if (!manualAudioLang.empty()) {
                aid = tracks::chooseAudio(list, manualAudioLang);
            } else {
                aid = tracks::chooseAudio(list, s.audioLanguage);
            }
            std::string audioLang;
            for (const auto &t: list) {
                if (t.kind == tracks::Kind::Audio && t.id == aid) {
                    audioLang = tracks::effectiveLanguage(t);
                }
            }
            int sid;
            if (manualSub >= 0) {
                sid = manualSub;
            } else if (manualSubLang == "off") {
                sid = 0;
            } else if (!manualSubLang.empty()) {
                sid = tracks::chooseSubtitle(list, tracks::SubtitleMode::Always, manualSubLang, audioLang);
            } else {
                std::string subLang = s.subtitleLanguage.empty() ? s.audioLanguage : s.subtitleLanguage;
                sid = tracks::chooseSubtitle(list, s.subtitleMode, subLang, audioLang);
            }
            LOG_I("player", "tracks chosen: audio %d (%s), subtitle %d", aid, audioLang.c_str(), sid);
            return {aid, sid};
        }

        void supervise(double now) {
            Playback &pb = app.playback();
            stability::Observation o;
            o.hadFrame = pb.started();
            switch (pb.state()) {
                case PlaybackState::Error:
                    o.phase = stability::Phase::Failed;
                    o.failKind = pb.failKind();
                    break;
                case PlaybackState::Ended: {
                    double d = tracker.duration();
                    if (pb.started() && (d <= 0 || tracker.position() >= d - END_MARGIN)) {
                        reachedEnd();
                        return;
                    }
                    o.phase = stability::Phase::Ended;   // the connection ended early: reconnect where we were
                    break;
                }
                case PlaybackState::Paused:
                    o.phase = stability::Phase::Running;
                    break;
                case PlaybackState::Buffering:
                case PlaybackState::Playing:
                    if (pb.started()) {
                        o.phase = stability::Phase::Running;
                        o.sinceProgress = pendingSeek ? 0 : pb.sinceProgress(now);
                        o.pausedForCache = pb.pausedForCache();
                        o.dataArriving = pb.info().cacheSpeed > 0;
                        break;
                    }
                    o.phase = stability::Phase::Opening;
                    o.sinceOpen = pb.openSeconds(now);
                    break;
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
                if (after == stability::Status::Failed) {
                    saveProgress(true);
                }
            }
            if ((recovery.waitingToReopen() || after == stability::Status::Failed) && pb.state() != PlaybackState::Idle
                && pb.state() != PlaybackState::Error && pb.state() != PlaybackState::Ended) {
                pb.stop();
            }
            if (action != stability::Action::None) {
                LOG_I("player", "reconnecting at %.3fs (attempt %d/%d)", tracker.position(), recovery.attempts(),
                      recovery.maxAttempts());
                open(tracker.position());
            }
        }

        void reachedEnd() {
            finished = true;
            Playback &pb = app.playback();
            tracker.record(app.now(), -1, true);   // completed: watched, leaves Continue Watching
            app.saveLibrary();
            LOG_I("player", "%s finished", item().id.c_str());
            pb.stop();
            bool next = isEpisode() && index + 1 < (int) queue.size();
            endTitle->setText(tr(next ? "player.up_next" : isEpisode() ? "player.last_episode" : "player.the_end"));
            if (next) {
                const VodItem &n = queue[(size_t) index + 1];
                endText->setText(fmt::episodeCode(n.season, n.episode) + "  " + episodeTitle(n));
                bool autoplay = app.settings().get().autoPlayNextEpisode;
                autoplayAt = autoplay ? app.now() + AUTOPLAY_COUNTDOWN : 0;
                endHint->setText(tr(autoplay ? "player.end_hint_play_now" : "player.end_hint_play_next"));
            } else {
                endText->setText(isEpisode() ? item().seriesName : item().title);
                endHint->setText(tr("player.end_hint_again"));
            }
            endPanel->setVisibility(Visibility::Visible);
            centre->setVisibility(Visibility::Hidden);
            setOverlay(false);
        }

        void endInput(const InputEvent &e) {
            if (e.repeat) {
                return;
            }
            if (e.button == PadButton::Circle) {
                exit();
            } else if (e.button == PadButton::Cross) {
                if (isEpisode() && index + 1 < (int) queue.size()) {
                    playNext();
                } else {
                    app.library().resetProgress(isEpisode() ? ContentType::Series : ContentType::Movie, item().id);
                    startItem(index, false);
                }
            }
        }

        void playNext() {
            if (index + 1 < (int) queue.size()) {
                startItem(index + 1, true);
            }
        }

        void exit() {
            if (!finished) {
                saveFinal("stop");
            }
            if (onExit) {
                onExit(item().id);
            }
            app.pop();
        }

        void seekBy(double seconds) {
            Playback &pb = app.playback();
            if (!pb.started() || !pb.info().seekable) {
                setOverlay(true);
                return;
            }
            double base = pendingSeek ? seekTarget : pb.position();
            double max = tracker.duration() > 0 ? tracker.duration() - 3 : base + seconds;
            seekTarget = std::max(0.0, std::min(base + seconds, max));
            pendingSeek = true;
            tracker.seekPending(seekTarget);
            seekCommitAt = app.now() + SEEK_COMMIT_DELAY;
            setOverlay(true);
            refresh(app.now());
        }

        // periodic progress of the current item; toDisk: also write history.json
        void saveProgress(bool toDisk) {
            if (tracker.record(app.now()) && toDisk) {
                app.saveLibrary();
            }
        }

        // stop / pause / next episode / app exit: the position mpv has right now, written to disk at once
        void saveFinal(const char *why) {
            double now = app.playback().queryPosition();
            if (tracker.record(app.now(), now)) {
                app.saveLibrary();
                LOG_I("player", "%s: progress saved at %.3fs of %.0fs", why, tracker.position(), tracker.duration());
            }
        }

        // ------------------------------------------------------------------ overlay
        void buildOverlay() {
            top = ui::box(this, FloatRect(0, 0, theme::SCREEN_W, 190), Color(0, 0, 0, 170), 0);
            title = ui::label(top, "", theme::TITLE, theme::SAFE_X, 44, ui::Weight::SemiBold);
            title->setMaxWidth(1400);
            subtitle = ui::label(top, "", theme::BODY, theme::SAFE_X, 112, ui::Weight::Regular, theme::textDim());
            subtitle->setMaxWidth(1400);
            clock = ui::label(top, "", theme::HEADING, 0, 48, ui::Weight::SemiBold);
            clock->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);

            bottom = ui::box(this, FloatRect(0, theme::SCREEN_H - 230, theme::SCREEN_W, 230), Color(0, 0, 0, 170), 0);
            const float barW = theme::SCREEN_W - 2 * theme::SAFE_X;
            barTrack = ui::box(bottom, FloatRect(theme::SAFE_X, 40, barW, 8), Color(255, 255, 255, 60), 4);
            barFill = ui::box(barTrack, FloatRect(0, 0, 8, 8), theme::accent(), 4);
            barThumb = new CircleShape(11);
            barThumb->setPointCount(20);
            barThumb->setFillColor(Color::White);
            barThumb->setOrigin(Origin::Center);
            barTrack->add(barThumb);
            timeLabel = ui::label(bottom, "", theme::BODY, theme::SAFE_X, 70, ui::Weight::SemiBold);
            chips = ui::label(bottom, "", theme::LABEL, 0, 74, ui::Weight::Regular, theme::textDim());
            chips->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);
            chips->setMaxWidth(1100);
            hints = new ui::HintBar();
            hints->setPosition(theme::SAFE_X, 150);
            bottom->add(hints);

            pausePill = ui::box(this, FloatRect((theme::SCREEN_W - 260) / 2, 460, 260, 90), Color(12, 16, 22, 210), 45);
            auto *pl = ui::label(pausePill, tr("player.paused"), theme::HEADING, 0, ui::Label::centerOffset(theme::HEADING, 90),
                                 ui::Weight::SemiBold);
            pl->setAlign(ui::Align::Center, 260);
            pausePill->setVisibility(Visibility::Hidden);

            pill = ui::box(this, FloatRect((theme::SCREEN_W - 460) / 2, 700, 460, 72), Color(12, 16, 22, 210), 36);
            pillText = ui::label(pill, tr("player.buffering"), theme::BODY, 84, ui::Label::centerOffset(theme::BODY, 72),
                                 ui::Weight::SemiBold);
            pillSpinner = new ui::Spinner(12);
            pillSpinner->setPosition(30, (72 - 12) / 2);
            pill->add(pillSpinner);
            pill->setVisibility(Visibility::Hidden);
        }

        void buildCentre() {
            const float w = 1160;
            centre = ui::box(this, FloatRect((theme::SCREEN_W - w) / 2, 380, w, 300), Color(12, 16, 22, 230),
                             theme::RADIUS);
            centreTitle = ui::label(centre, "", theme::HEADING, 0, 52, ui::Weight::SemiBold);
            centreTitle->setAlign(ui::Align::Center, w);
            centreTitle->setMaxWidth(w - 80);
            centreText = ui::label(centre, "", theme::BODY, 0, 118, ui::Weight::Regular, theme::textDim());
            centreText->setAlign(ui::Align::Center, w);
            centreText->setMaxWidth(w - 80);
            centreText->setMaxLines(2);
            centreHint = ui::label(centre, "", theme::LABEL, 0, 222, ui::Weight::Regular, theme::textMuted());
            centreHint->setAlign(ui::Align::Center, w);
            spinner = new ui::Spinner(18);
            spinner->setPosition((w - 18 * 3.6f) / 2, 230);
            centre->add(spinner);

            endPanel = ui::box(this, FloatRect((theme::SCREEN_W - w) / 2, 380, w, 300), Color(12, 16, 22, 235),
                               theme::RADIUS);
            endTitle = ui::label(endPanel, "", theme::LABEL, 0, 50, ui::Weight::SemiBold, theme::accent());
            endTitle->setAlign(ui::Align::Center, w);
            endText = ui::label(endPanel, "", theme::HEADING, 0, 100, ui::Weight::SemiBold);
            endText->setAlign(ui::Align::Center, w);
            endText->setMaxWidth(w - 80);
            endHint = ui::label(endPanel, "", theme::LABEL, 0, 222, ui::Weight::Regular, theme::textMuted());
            endHint->setAlign(ui::Align::Center, w);
            endPanel->setVisibility(Visibility::Hidden);
        }

        void buildInfo() {
            info = ui::box(this, FloatRect(theme::SAFE_X, 220, 660, 560), Color(12, 16, 22, 230), theme::RADIUS);
            ui::label(info, tr("info.title"), theme::HEADING, 36, 30, ui::Weight::SemiBold);
            infoText = ui::label(info, "", theme::LABEL, 36, 96, ui::Weight::Regular, theme::textDim());
            infoText->setMaxWidth(600);
            infoText->setMaxLines(15);
            info->setVisibility(app.settings().get().showTechnicalInfo ? Visibility::Visible : Visibility::Hidden);
        }

        void setOverlay(bool show) {
            overlayVisible = show;
            top->setVisibility(show ? Visibility::Visible : Visibility::Hidden);
            bottom->setVisibility(show ? Visibility::Visible : Visibility::Hidden);
        }

        std::string trackSummary() {
            Playback &pb = app.playback();
            const auto &list = pb.trackList();
            std::string s;
            int audioCount = 0;
            int subCount = 0;
            for (const auto &t: list) {
                (t.kind == tracks::Kind::Audio ? audioCount : subCount)++;
            }
            const tracks::Track *a = tracks::selected(list, tracks::Kind::Audio);
            const tracks::Track *sub = tracks::selected(list, tracks::Kind::Subtitle);
            if (a) {
                s += tr("player.chip_audio", {tracks::label(*a, ordinalOf(*a))});
            }
            if (subCount > 0) {
                s += std::string(s.empty() ? "" : "   \xC2\xB7   ")
                     + tr("player.chip_subtitles", {sub ? tracks::label(*sub, ordinalOf(*sub)) : tr("common.off")});
            }
            return s;
        }

        void refresh(double now) {
            Playback &pb = app.playback();
            const VodItem &it = item();
            const StreamInfo &si = pb.info();
            std::string source = it.localPath.empty() ? "" : "   \xC2\xB7   " + tr("player.offline_copy");
            if (isEpisode()) {
                title->setText(it.seriesName);
                subtitle->setText(fmt::episodeCode(it.season, it.episode) + "  \xE2\x80\x94  " + episodeTitle(it) + source);
            } else {
                title->setText(it.title);
                std::string year = it.year > 0 ? std::to_string(it.year) : "";
                subtitle->setText(!year.empty() ? year + source : it.localPath.empty() ? "" : tr("player.offline_copy"));
            }
            clock->setText(clockx::localTime());

            double duration = tracker.duration() > 0 ? tracker.duration() : pb.duration();
            double position = pendingSeek ? seekTarget : (pb.started() ? pb.position() : tracker.position());
            float frac = (float) progress::fraction(position, duration);
            float w = barTrack->getSize().x;
            barFill->setSize(std::max(8.0f, std::round(w * frac)), 8);
            barThumb->setPosition(std::round(w * frac), 4);
            std::string time = fmt::clockPair(position, duration);
            if (pendingSeek) {
                time = "\xE2\x86\x92 " + time;   // arrow: target of the pending seek
            }
            timeLabel->setText(time);
            std::string res = fmt::resolution(si.width, si.height);
            std::string chipText = trackSummary();
            if (!res.empty()) {
                chipText = res + (chipText.empty() ? "" : "   \xC2\xB7   " + chipText);
            }
            chips->setText(chipText);
            std::vector<std::pair<ui::Glyph, std::string>> h = {
                    {ui::Glyph::Cross, tr(pb.state() == PlaybackState::Paused ? "common.play" : "player.pause")},
                    {ui::Glyph::DPad, tr("player.seek_10")}, {ui::Glyph::L2, ""}, {ui::Glyph::R2, tr("player.seek_60")},
                    {ui::Glyph::Options, tr("player.options")}, {ui::Glyph::Triangle, tr("player.info")}};
            if (isEpisode()) {
                h.push_back({ui::Glyph::R1, tr("player.next_episode")});
            }
            h.push_back({ui::Glyph::Circle, tr("common.back")});
            hints->setHints(h);

            stability::Status rs = recovery.status();
            pausePill->setVisibility(pb.state() == PlaybackState::Paused && !panelOpen ? Visibility::Visible
                                                                                       : Visibility::Hidden);
            bool centreShown = !finished && (rs == stability::Status::Opening || rs == stability::Status::Reconnecting
                                             || rs == stability::Status::Refused || rs == stability::Status::Failed);
            centre->setVisibility(centreShown ? Visibility::Visible : Visibility::Hidden);
            bool spinning = centreShown && (rs == stability::Status::Opening || rs == stability::Status::Reconnecting);
            spinner->setVisibility(spinning ? Visibility::Visible : Visibility::Hidden);
            centreHint->setVisibility(spinning ? Visibility::Hidden : Visibility::Visible);
            centreTitle->setColor(rs == stability::Status::Failed ? theme::danger() : theme::text());
            if (spinning) {
                spinner->tick(now);
            }
            int secondsLeft = (int) std::ceil(recovery.secondsLeft(now));
            std::string attempt = tr("player.attempt_of", {std::to_string(recovery.attempts()),
                                                           std::to_string(recovery.maxAttempts())});
            if (rs == stability::Status::Opening) {
                centreTitle->setText(isEpisode() ? it.seriesName : it.title);
                centreText->setText(tracker.position() > 1 ? tr("player.resuming_at", {fmt::clock(tracker.position())})
                                                           : tr("player.opening"));
            } else if (rs == stability::Status::Reconnecting) {
                centreTitle->setText(tr("player.reconnecting"));
                centreText->setText(secondsLeft > 0 ? tr("player.next_attempt", {std::to_string(secondsLeft), attempt})
                                                    : tr("player.attempt", {attempt}));
            } else if (rs == stability::Status::Refused) {
                centreTitle->setText(tr("player.http_403"));
                centreText->setText(tr("player.retrying_in", {std::to_string(secondsLeft), attempt}));
                centreHint->setText(tr("player.hint_retry_now"));
            } else if (rs == stability::Status::Failed) {
                bool stopped = pb.state() == PlaybackState::Error;
                centreTitle->setText(stopped && !pb.errorMessage().empty() ? pb.errorMessage() : tr("player.title_unavailable"));
                centreText->setText(recovery.attempts() > 0 ? tr("player.gave_up", {std::to_string(recovery.attempts())}) : "");
                centreHint->setText(tr("player.hint_retry"));
            }
            bool pillShown = !finished && rs == stability::Status::Buffering;
            pill->setVisibility(pillShown ? Visibility::Visible : Visibility::Hidden);
            if (pillShown) {
                pillSpinner->tick(now);
            }

            if (info->isVisible()) {
                std::string reason = recovery.reasonText();
                std::vector<std::pair<const char *, std::string>> lines = {
                        {"info.container", si.format},
                        {"info.source", tr(it.localPath.empty() ? "info.source_stream" : "info.source_file")},
                        {"info.resolution", si.width > 0 ? std::to_string(si.width) + " x " + std::to_string(si.height) : "-"},
                        {"info.frame_rate", si.fps > 0 ? diag::format("%.2f fps", si.fps) : "-"},
                        {"info.video_codec", si.videoCodec.empty() ? "-" : si.videoCodec},
                        {"info.pixel_format", si.pixelFormat.empty() ? "-" : si.pixelFormat},
                        {"info.audio_codec", si.audioCodec.empty() ? "-" : si.audioCodec},
                        {"info.audio", si.sampleRate > 0 ? diag::format("%d Hz, ", si.sampleRate) + tracks::channelsName(si.channels) : "-"},
                        {"info.buffer", diag::format("%.1f s", si.cacheSeconds)},
                        {"info.network", si.cacheSpeed > 0 ? diag::format("%lld KB/s", si.cacheSpeed / 1000) : "-"},
                        {"info.dropped", std::to_string(si.droppedFrames)},
                        {"info.tracks", tr("info.tracks_value", {std::to_string(countTracks(tracks::Kind::Audio)),
                                                                 std::to_string(countTracks(tracks::Kind::Subtitle))})},
                        {"info.display", geometryText()},
                        {"info.stability", tr(stabilityKey(preset))},
                        {"info.state", tr(statusKey(rs))},
                        {"info.recovery", std::to_string(recovery.attempts()) + " / " + std::to_string(recovery.maxAttempts())
                                          + (reason.empty() ? "" : ", " + tr("info.last", {reason}))}};
                std::string text;
                for (const auto &l: lines) {
                    text += (text.empty() ? "" : "\n") + tr(l.first) + ":  " + l.second;
                }
                infoText->setText(text);
            }
        }

        int countTracks(tracks::Kind kind) {
            int n = 0;
            for (const auto &t: app.playback().trackList()) {
                n += t.kind == kind;
            }
            return n;
        }

        // ------------------------------------------------------------------ options panel
        // the track's position among the tracks of its kind (labels "Track 2" for untagged tracks)
        int ordinalOf(const tracks::Track &track) {
            int n = 0;
            for (const auto &t: app.playback().trackList()) {
                if (t.kind == track.kind) {
                    n++;
                    if (t.id == track.id) {
                        return n;
                    }
                }
            }
            return track.id;
        }

        enum class Row {
            AudioMenu,
            SubtitleMenu,
            VideoMenu,
            SubtitleSize,
            SubtitlePosition,
            SubtitleShadow,
            TechInfo,
            AudioTrack,     // level 1
            SubtitleTrack,  // level 2 (id 0 = off)
            Aspect,         // level 3: video geometry
            Crop,
            Zoom,
            PositionX,
            PositionY,
            ResetGeometry,
            DisplayDefault, // the current aspect / crop / zoom become the Settings defaults
            BackRow
        };

        // the Options panel's menus (panelLevel)
        enum Menu {
            MENU_MAIN = 0,
            MENU_AUDIO = 1,
            MENU_SUBTITLES = 2,
            MENU_VIDEO = 3
        };

        struct PanelItem {
            Row row;
            int trackId = 0;
        };

        void buildPanel() {
            panel = ui::box(this, FloatRect(theme::SCREEN_W - PANEL_W, 0, PANEL_W, theme::SCREEN_H),
                            Color(14, 18, 26, 240), 0);
            panelTitle = ui::label(panel, "", theme::TITLE, 48, 64, ui::Weight::SemiBold);
            panelList = new ui::ListView(FloatRect(40, 170, PANEL_W - 70, 760), 84, 8, this);
            panel->add(panelList);
            auto *ph = new ui::HintBar();
            ph->setPosition(48, theme::SCREEN_H - theme::SAFE_Y - 40);
            ph->setHints({{ui::Glyph::Cross, tr("common.select")}, {ui::Glyph::DPad, tr("settings.hint_change")},
                          {ui::Glyph::Circle, tr("common.back")}});
            panel->add(ph);
            panel->setVisibility(Visibility::Hidden);
        }

        int count() override { return (int) panelItems.size(); }

        C2DObject *createRow(float w, float h) override {
            PanelRow r;
            r.bg = ui::box(nullptr, FloatRect(0, 0, w, h), Color::Transparent, theme::RADIUS_SMALL);
            r.check = ui::label(r.bg, "", theme::BODY, 20, ui::Label::centerOffset(theme::BODY, h) - 12,
                                ui::Weight::SemiBold, theme::accent());
            r.name = ui::label(r.bg, "", theme::BODY, 64, ui::Label::centerOffset(theme::BODY, h) - 12);
            r.name->setMaxWidth(w - 64 - 24);
            r.detail = ui::label(r.bg, "", theme::CAPTION, 64, ui::Label::centerOffset(theme::BODY, h) + 22,
                                 ui::Weight::Regular, theme::textMuted());
            r.detail->setMaxWidth(w - 64 - 24);
            panelRows.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int i, bool, bool focused) override {
            const PanelItem &pi = panelItems[(size_t) i];
            std::string name, detail;
            bool checked = false;
            const Settings &s = app.settings().get();
            const auto &list = app.playback().trackList();
            switch (pi.row) {
                case Row::AudioMenu: {
                    const tracks::Track *a = tracks::selected(list, tracks::Kind::Audio);
                    name = tr("options.audio");
                    detail = a ? tracks::label(*a, ordinalOf(*a)) + "  \xC2\xB7  " + tracks::details(*a)
                               : list.empty() ? tr("options.available_later") : "-";
                    break;
                }
                case Row::SubtitleMenu: {
                    const tracks::Track *sub = tracks::selected(list, tracks::Kind::Subtitle);
                    name = tr("options.subtitles");
                    detail = sub ? tracks::label(*sub, ordinalOf(*sub)) : countTracks(tracks::Kind::Subtitle) > 0
                                                                          ? tr("common.off") : tr("options.no_subtitles");
                    break;
                }
                case Row::VideoMenu:
                    name = tr("options.video");
                    detail = geometryText();
                    break;
                case Row::Aspect:
                    name = tr("geometry.aspect") + ":  " + screens::aspectName(geometry.aspect);
                    detail = tr("geometry.aspect_desc");
                    break;
                case Row::Crop:
                    name = tr("geometry.crop") + ":  " + screens::cropName(geometry.crop);
                    detail = tr("geometry.crop_desc");
                    break;
                case Row::Zoom:
                    name = tr("geometry.zoom") + ":  " + screens::zoomName(geometry.zoom);
                    break;
                case Row::PositionX:
                    name = tr("geometry.pos_x") + ":  " + screens::positionName(geometry.posX, true);
                    detail = tr("geometry.position_desc");
                    break;
                case Row::PositionY:
                    name = tr("geometry.pos_y") + ":  " + screens::positionName(geometry.posY, false);
                    detail = tr("geometry.position_desc");
                    break;
                case Row::ResetGeometry:
                    name = tr("geometry.reset");
                    detail = tr("geometry.reset_desc");
                    break;
                case Row::SubtitleSize:
                    name = tr("subtitle.size");
                    detail = tr(SIZE_KEYS[std::min(std::max(s.subtitleSize, 0), 2)]);
                    break;
                case Row::SubtitlePosition:
                    name = tr("subtitle.position");
                    detail = tr(POSITION_KEYS[s.subtitlePosition == 1 ? 1 : 0]);
                    break;
                case Row::SubtitleShadow:
                    name = tr("subtitle.shadow");
                    detail = tr(s.subtitleShadow ? "common.on" : "common.off");
                    break;
                case Row::TechInfo:
                    name = tr("options.tech_info");
                    detail = tr(info->isVisible() ? "options.shown" : "options.hidden");
                    break;
                case Row::DisplayDefault: {
                    const display::Geometry d = s.defaultGeometry();
                    bool same = d.aspect == geometry.aspect && d.crop == geometry.crop && d.zoom == geometry.zoom;
                    name = tr("options.display_default");
                    detail = tr(same ? "options.display_is_default" : "options.display_make_default");
                    break;
                }
                case Row::AudioTrack:
                case Row::SubtitleTrack: {
                    if (pi.trackId == 0) {
                        name = tr("common.off");
                        checked = tracks::selected(list, tracks::Kind::Subtitle) == nullptr;
                        break;
                    }
                    int ordinal = 0;
                    tracks::Kind kind = pi.row == Row::AudioTrack ? tracks::Kind::Audio : tracks::Kind::Subtitle;
                    for (const auto &t: list) {
                        if (t.kind != kind) {
                            continue;
                        }
                        ordinal++;
                        if (t.id == pi.trackId) {
                            name = tracks::label(t, ordinal);
                            detail = tracks::details(t);
                            checked = t.selected;
                        }
                    }
                    break;
                }
                default:
                    name = tr("common.back");
                    break;
            }
            for (auto &r: panelRows) {
                if (r.bg != obj) {
                    continue;
                }
                r.check->setText(checked ? "\xE2\x9C\x93" : "");
                r.name->setText(name);
                r.detail->setText(detail);
                r.name->setColor(focused ? Color::White : theme::text());
                r.bg->setFillColor(focused ? theme::rowFocus() : Color::Transparent);
                r.bg->setOutlineColor(theme::accent());
                r.bg->setOutlineThickness(focused ? 3 : 0);
            }
        }

        void openPanel() {
            panelOpen = true;
            showMenu(0);
            panel->setVisibility(Visibility::Visible);
            setOverlay(false);
            pausePill->setVisibility(Visibility::Hidden);
        }

        void closePanel() {
            panelOpen = false;
            panel->setVisibility(Visibility::Hidden);
            setOverlay(true);
        }

        void showMenu(int level, int selectRow = 0) {
            panelLevel = level;
            panelItems.clear();
            if (level == MENU_MAIN) {
                panelTitle->setText(tr("options.title"));
                for (Row r: {Row::AudioMenu, Row::SubtitleMenu, Row::VideoMenu, Row::SubtitleSize,
                             Row::SubtitlePosition, Row::SubtitleShadow, Row::TechInfo}) {
                    panelItems.push_back({r, 0});
                }
            } else if (level == MENU_VIDEO) {
                panelTitle->setText(tr("options.video"));
                for (Row r: {Row::Aspect, Row::Crop, Row::Zoom, Row::PositionX, Row::PositionY, Row::ResetGeometry,
                             Row::DisplayDefault}) {
                    panelItems.push_back({r, 0});
                }
            } else {
                tracks::Kind kind = level == MENU_AUDIO ? tracks::Kind::Audio : tracks::Kind::Subtitle;
                panelTitle->setText(tr(level == MENU_AUDIO ? "options.audio" : "options.subtitles"));
                if (kind == tracks::Kind::Subtitle) {
                    panelItems.push_back({Row::SubtitleTrack, 0});
                }
                int selectedIndex = -1;
                for (const auto &t: app.playback().trackList()) {
                    if (t.kind == kind) {
                        if (t.selected) {
                            selectedIndex = (int) panelItems.size();
                        }
                        panelItems.push_back({kind == tracks::Kind::Audio ? Row::AudioTrack : Row::SubtitleTrack, t.id});
                    }
                }
                if (panelItems.empty()) {
                    panelItems.push_back({Row::BackRow, 0});
                }
                if (selectRow == 0 && selectedIndex >= 0) {
                    selectRow = selectedIndex;
                }
            }
            panelList->setSelected(selectRow);
            panelList->reload();
        }

        void panelInput(const InputEvent &e) {
            switch (e.button) {
                case PadButton::Up:
                    panelList->moveSelection(-1);
                    return;
                case PadButton::Down:
                    panelList->moveSelection(1);
                    return;
                case PadButton::Circle:
                case PadButton::Options:
                    if (e.repeat) {
                        return;
                    }
                    if (panelLevel != MENU_MAIN) {
                        // back to the main menu, on the row that opened this one
                        showMenu(MENU_MAIN, panelLevel == MENU_AUDIO ? 0 : panelLevel == MENU_SUBTITLES ? 1 : 2);
                    } else {
                        closePanel();
                    }
                    return;
                case PadButton::Left:
                case PadButton::Right:
                case PadButton::Cross:
                    if (!e.repeat || e.button != PadButton::Cross) {
                        activate(panelItems[(size_t) panelList->selected()], e.button == PadButton::Left ? -1 : 1,
                                 e.button == PadButton::Cross);
                    }
                    return;
                default:
                    return;
            }
        }

        // delta: -1 Left, 1 Right / Cross. cross: Cross cycles (wraps) where Left / Right stop at the ends
        void activate(const PanelItem &pi, int delta, bool cross) {
            Settings &s = app.settings().get();
            Playback &pb = app.playback();
            bool appearance = false;
            switch (pi.row) {
                case Row::AudioMenu:
                    if (delta > 0) {
                        showMenu(MENU_AUDIO);
                    }
                    return;
                case Row::SubtitleMenu:
                    if (delta > 0) {
                        showMenu(MENU_SUBTITLES);
                    }
                    return;
                case Row::VideoMenu:
                    if (delta > 0) {
                        showMenu(MENU_VIDEO);
                    }
                    return;
                // geometry: applied at once, without a reload (position, pause state and tracks stay)
                case Row::Aspect:
                    geometry.aspect = display::stepAspect(geometry.aspect, delta);
                    applyGeometry();
                    break;
                case Row::Crop:
                    geometry.crop = display::stepCrop(geometry.crop, delta);
                    applyGeometry();
                    break;
                case Row::Zoom: {
                    const auto &steps = display::zoomSteps();
                    geometry.zoom = cross && geometry.zoom == steps.back() ? steps.front()
                                                                           : display::stepZoom(geometry.zoom, delta);
                    applyGeometry();
                    break;
                }
                case Row::PositionX:
                case Row::PositionY: {
                    int &p = pi.row == Row::PositionX ? geometry.posX : geometry.posY;
                    p = cross && p == display::POSITION_STEPS ? -display::POSITION_STEPS
                                                              : display::stepPosition(p, delta);
                    applyGeometry();
                    break;
                }
                case Row::ResetGeometry:
                    if (delta < 0) {
                        return;
                    }
                    geometry = display::defaults();
                    applyGeometry();
                    app.toast(tr("geometry.reset_done"));
                    break;
                case Row::DisplayDefault:
                    if (delta < 0) {
                        return;
                    }
                    s.videoAspect = geometry.aspect;
                    s.videoCrop = geometry.crop;
                    s.zoomPercent = geometry.zoom;
                    {
                        std::string err;
                        if (!app.settings().save(&err)) {
                            LOG_E("settings", "save failed: %s", err.c_str());
                        }
                    }
                    app.toast(tr("options.display_saved"), ToastKind::Success);
                    break;
                case Row::SubtitleSize:
                    s.subtitleSize = std::min(2, std::max(0, s.subtitleSize + delta));
                    appearance = true;
                    break;
                case Row::SubtitlePosition:
                    s.subtitlePosition = s.subtitlePosition == 1 ? 0 : 1;
                    appearance = true;
                    break;
                case Row::SubtitleShadow:
                    s.subtitleShadow = !s.subtitleShadow;
                    appearance = true;
                    break;
                case Row::TechInfo:
                    info->setVisibility(info->isVisible() ? Visibility::Hidden : Visibility::Visible);
                    break;
                case Row::AudioTrack:
                    if (delta < 0) {
                        return;
                    }
                    manualAudio = pi.trackId;
                    for (const auto &t: pb.trackList()) {
                        if (t.kind == tracks::Kind::Audio && t.id == pi.trackId) {
                            manualAudioLang = tracks::effectiveLanguage(t);
                        }
                    }
                    pb.selectAudio(pi.trackId);
                    break;
                case Row::SubtitleTrack:
                    if (delta < 0) {
                        return;
                    }
                    manualSub = pi.trackId;
                    manualSubLang = "off";
                    for (const auto &t: pb.trackList()) {
                        if (t.kind == tracks::Kind::Subtitle && t.id == pi.trackId) {
                            manualSubLang = tracks::effectiveLanguage(t);
                        }
                    }
                    pb.selectSubtitle(pi.trackId);
                    break;
                default:
                    showMenu(MENU_MAIN);
                    return;
            }
            if (appearance) {
                pb.applyOptions(tracks::appearanceOptions(s.subtitleSize, s.subtitlePosition, s.subtitleShadow));
                std::string err;
                if (!app.settings().save(&err)) {
                    LOG_E("settings", "save failed: %s", err.c_str());
                }
            }
            panelList->reload();
            refresh(app.now());
        }

        // the video geometry of this playback (mpv properties at runtime: no reload, no seek, tracks untouched)
        void applyGeometry() {
            LOG_I("player", "geometry: aspect %s, crop %s, zoom %d%%, position %d/%d", display::aspectKey(geometry.aspect),
                  display::cropKey(geometry.crop), geometry.zoom, geometry.posX, geometry.posY);
            app.playback().applyOptions(display::mpvOptions(geometry, (int) theme::SCREEN_W, (int) theme::SCREEN_H));
        }

        // "Auto / Source  ·  None  ·  100%" (+ the position when it is not centred)
        std::string geometryText() const {
            std::string text = screens::geometrySummary(geometry);
            if (geometry.posX != 0) {
                text += "  \xC2\xB7  " + screens::positionName(geometry.posX, true);
            }
            if (geometry.posY != 0) {
                text += "  \xC2\xB7  " + screens::positionName(geometry.posY, false);
            }
            return text;
        }

        struct PanelRow {
            RectangleShape *bg;
            ui::Label *check;
            ui::Label *name;
            ui::Label *detail;
        };

        std::vector<VodItem> queue;
        int index;
        bool resumeOnStart;
        std::function<void(const std::string &)> onExit;
        VodProgress tracker;
        StabilityPreset preset = StabilityPreset::Balanced;
        stability::Recovery recovery;
        bool pendingSeek = false;
        double seekTarget = 0;
        double seekCommitAt = 0;
        bool finished = false;
        double autoplayAt = 0;
        bool overlayVisible = true;
        double lastActivity = 0;
        bool wasPlaying = false;
        double lastRefresh = 0;
        bool subtitleWarningShown = false;
        bool languagesRecorded = false;
        // track choices made in the Options panel (this playback); languages carry over to the next episode
        int manualAudio = -1;
        int manualSub = -2;
        std::string manualAudioLang;
        std::string manualSubLang;
        display::Geometry geometry;   // this playback's (starts as the Settings defaults, centred)
        std::string previousProfile;   // library profile before a download of another profile was played

        VideoTexture *video;
        RectangleShape *top;
        RectangleShape *bottom;
        RectangleShape *barTrack;
        RectangleShape *barFill;
        CircleShape *barThumb;
        RectangleShape *pausePill;
        RectangleShape *pill;
        RectangleShape *centre;
        RectangleShape *endPanel;
        RectangleShape *info;
        RectangleShape *panel;
        ui::Label *title;
        ui::Label *subtitle;
        ui::Label *clock;
        ui::Label *timeLabel;
        ui::Label *chips;
        ui::Label *pillText;
        ui::Label *centreTitle;
        ui::Label *centreText;
        ui::Label *centreHint;
        ui::Label *endTitle;
        ui::Label *endText;
        ui::Label *endHint;
        ui::Label *infoText;
        ui::Label *panelTitle;
        ui::Spinner *spinner;
        ui::Spinner *pillSpinner;
        ui::HintBar *hints;
        ui::ListView *panelList;
        std::vector<PanelItem> panelItems;
        std::vector<PanelRow> panelRows;
        int panelLevel = 0;
        bool panelOpen = false;
    };
}

namespace screens {
    Screen *makeVodPlayer(App &app, const std::vector<VodItem> &queue, int index, bool resume,
                          std::function<void(const std::string &)> onExit) {
        if (queue.empty()) {
            return makeSection(app, tr("player.title"), tr("player.nothing"));
        }
        int i = std::min(std::max(index, 0), (int) queue.size() - 1);
        return new VodPlayerScreen(app, queue, i, resume, std::move(onExit));
    }
}
