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
//   - episodes: L1/R1 previous/next, an "Up next" panel at the end (app/auto_next.h for both moments below):
//     * Continue Watching: once 45 s or less remain, the episode is complete for Continue Watching (CompletionLatch)
//       and the next episode becomes the series' Continue Watching entry at 0 s right away (LibraryStore::markUpNext),
//       whatever happens next - exit, end of file, Auto-play off, Watch credits.
//     * The Next Episode card (Settings > Playback > Auto-play next episode, on by default): 45 s before the end a
//       compact card bottom right (NEXT EPISODE / S02 E05 · title / [Play now]  Watch credits / a thin bar) counts
//       its 7 seconds and then starts the next episode without waiting for the end of file. Play now (Cross /
//       Enter) starts it at once, Watch credits (Circle / Esc) dismisses it for this playback. Only while it is shown
//       do Cross / Circle mean that. An end of file is the transition too, exactly once; playback errors never are.
//       Every transition is startItem(), the same path as L1 / R1.
//     Every entry point builds the queue with screens::playSeriesEpisode; when the series' catalog was not in
//     memory (Continue Watching right after start), the player asks for it once and completes its queue then.
//   - video geometry (player/display): Aspect Ratio, Crop / Fill, Zoom and Position in the Options panel's Video
//     menu, mpv properties set at runtime (no reload: position, pause state and tracks stay). The Settings
//     defaults apply when the player opens; a change in the panel lasts for this playback only, unless the
//     user chooses "Set as default" there
//   - offline: a downloaded item (VodItem::localPath) opens the local file through the same Playback /
//     mpv path; tracks, video geometry, resume and progress work identically. Progress is kept in the download's
//     profile (the library switches to it while the player is open)
//   - downloads pause while the player is open (App::setPlaybackActive): decoding comes first
//   - Stalker / MAG sources: every open asks the portal for a fresh address (create_link) and plays it with the MAG
//     User-Agent; an episode without its play command waits for the series' episode list
//   - Local Media (VodItem::localMedia): a file from the console's storage, same player and Options panel, but no
//     progress, history, favorites or next episode (it belongs to no source)

#include <cctype>
#include <cmath>

#include "common.h"
#include "../core/format.h"
#include "../iptv/xtream.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../app/auto_next.h"
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
            setFillColor(theme::videoBackground());
            geometry = app.settings().get().defaultGeometry();
            app.setPlaybackActive(true);
            video = new VideoTexture(app.playback().backend(), {theme::SCREEN_W, theme::SCREEN_H});
            add(video);
            buildOverlay();
            buildCentre();
            buildPanel();
            buildInfo();
            buildNextPanel();
        }

        ~VodPlayerScreen() override {
            linkToken.cancel();
            app.playback().stop();
            app.playback().setUserAgent("");
            app.setPlaybackActive(false);
            if (!previousProfile.empty()) {
                app.library().setProfile(previousProfile);   // back to the signed-in profile
            }
        }

        const char *name() const override { return "vod-player"; }

        bool animating() const override { return true; }

        bool ticksUnderModal() const override { return true; }   // under the choice picker: playback goes on

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
            pb.setRenderTiming(video->renderTiming());
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
                updateCompletion(now);
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
            completeQueue();
            retryAwaitingInfo();
            if ((!finished || countdown.afterEnd()) && updateCountdown()) {
                if (countdown.afterEnd() || finished) {
                    startNext("card countdown after the end of file");   // log reason i18n-exempt
                } else {
                    advance("card countdown");   // log reason i18n-exempt
                }
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
            // the countdown panel has Cross / Circle only while it is shown; everything else works as always
            if (countdown.visible() && (e.button == PadButton::Cross || e.button == PadButton::Circle)) {
                if (e.repeat) {
                    return;
                }
                if (e.button == PadButton::Cross && countdown.playNow()) {
                    advance("play now");   // log reason i18n-exempt
                } else if (e.button == PadButton::Circle && countdown.cancel()) {
                    LOG_I("player", "next episode card: watch credits (no automatic next episode for this playback)");
                    showNextPanel(false);
                    refresh(app.now());
                }
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
                    if (!e.repeat && !item().localMedia) {   // a local file is nobody's favorite
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
            countdown.reset();
            completion.reset();
            upNextDone = false;
            showNextPanel(false);
            resolveNext();
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
            std::string container = it.extension.empty() ? "VOD" : it.extension;
            for (auto &c: container) {
                c = (char) toupper((unsigned char) c);
            }
            linkToken.cancel();
            awaitingInfoStart = -1;
            if (it.localPath.empty() && !it.localMedia && app.session().profile.isStalker()) {
                openStalker(start, container);
                video->resetFrameStats();
                return;
            }
            pb.setUserAgent("");
            // a downloaded copy / a local file plays from the disk (same backend, no provider request)
            std::string url = !it.localPath.empty() ? it.localPath
                              : isEpisode() ? xtream::seriesUrl(app.session().profile, it.id, it.extension)
                                            : xtream::movieUrl(app.session().profile, it.id, it.extension);
            pb.open(url, container, openOptions(start));
            video->resetFrameStats();
        }

        Playback::OpenOptions openOptions(double start) {
            Playback::OpenOptions o;
            o.start = start;
            o.chooseTracks = [this](const std::vector<tracks::Track> &list) { return chooseTracks(list); };
            return o;
        }

        // Stalker: the portal creates the address (single-use play token) for every open
        void openStalker(double start, const std::string &container) {
            Playback &pb = app.playback();
            pb.prepare(container);
            const VodItem &it = item();
            int at = index;
            std::string id = it.id;
            auto done = [this, at, id, start, container](const sources::LinkOutcome &r) {
                if (index != at || item().id != id) {
                    return;   // another item started meanwhile
                }
                Playback &p = app.playback();
                if (!r.ok) {
                    p.failBeforeOpen(r.message, "create_link failed", true);   // i18n-exempt: technical value
                    refresh(app.now());
                    return;
                }
                p.setUserAgent(r.userAgent);
                p.open(r.url, container, openOptions(start));
                refresh(app.now());
            };
            if (!isEpisode()) {
                linkToken = app.stalker().resolveMovie(app.session().profile, it.id, done);
                return;
            }
            std::string cmd = it.cmd;
            int series = it.linkSeries;
            if (cmd.empty()) {
                // an entry point without the episode list (Continue Watching right after start): take the command
                // from the series' episodes, asking for them once
                if (auto info = app.vod().seriesInfo(it.seriesId)) {
                    for (const auto &season: info->seasons) {
                        for (const auto &ep: season.episodes) {
                            if (ep.id == it.id) {
                                cmd = ep.cmd;
                                series = ep.linkSeries;
                            }
                        }
                    }
                }
            }
            if (cmd.empty()) {
                if (awaitingInfoFor != it.seriesId) {
                    awaitingInfoFor = it.seriesId;
                    app.vod().requestSeriesInfo(app.session().profile, it.seriesId);
                    awaitingInfoStart = start;
                    awaitingInfoGen = app.vod().generation();
                    LOG_I("player", "episode %s: waiting for the series' episode list (play command)", it.id.c_str());
                    return;
                }
                pb.failBeforeOpen(tr("stalker.link.unknown"), "episode play command unknown", false);   // i18n-exempt: technical value
                return;
            }
            linkToken = app.stalker().resolveEpisode(app.session().profile, cmd, series, done);
        }

        // Stalker: the episode list asked for by openStalker() arrived (or failed)
        void retryAwaitingInfo() {
            if (awaitingInfoStart < 0 || app.vod().generation() == awaitingInfoGen) {
                return;
            }
            awaitingInfoGen = app.vod().generation();
            const VodItem &it = item();
            if (app.vod().seriesInfo(it.seriesId)) {
                double start = awaitingInfoStart;
                awaitingInfoStart = -1;
                open(start);
            } else if (!app.vod().detailError("s" + it.seriesId).empty()) {
                awaitingInfoStart = -1;
                app.playback().failBeforeOpen(app.vod().detailError("s" + it.seriesId), "series info failed", true);   // i18n-exempt: technical value
            }
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
            persistUpNext("end of file");          // log reason i18n-exempt
            app.saveLibrary();
            LOG_I("player", "%s finished", item().id.c_str());
            pb.stop();
            const bool wasShown = countdown.visible();
            logEndOfFile(wasShown);
            // the Next Episode card after the end (also when the end came before its 45 s window: a reported duration
            // longer than the media, or no duration at all): Auto-play ON counts its 7 s, OFF waits for Play now
            if (countdown.endOfFile(nextIdx >= 0, autoPlay(), app.now())) {
                centre->setVisibility(Visibility::Hidden);
                setOverlay(false);
                showNextPanel(true);
                layoutNextActions();
                refresh(app.now());
                return;
            }
            showEndPanel();
        }

        // the end panel: Up next (after Watch credits / Cancel), or the series' last episode / a movie's end
        void showEndPanel() {
            showNextPanel(false);
            bool next = isEpisode() && nextIdx >= 0;   // the provider's next episode (never a guess)
            endTitle->setText(tr(next ? "player.up_next" : isEpisode() ? "player.last_episode" : "player.the_end"));
            if (next) {
                const VodItem &n = queue[(size_t) nextIdx];
                endText->setText(fmt::episodeCode(n.season, n.episode) + "  " + episodeTitle(n));
                setCentredHints(endHints, {{A::Confirm, tr("player.play_next")}, {A::Back, tr("common.back")}});
            } else {
                endText->setText(isEpisode() ? item().seriesName : item().title);
                setCentredHints(endHints, {{A::Confirm, tr("player.watch_again")}, {A::Back, tr("common.back")}});
            }
            endPanel->setVisibility(Visibility::Visible);
            centre->setVisibility(Visibility::Hidden);
            setOverlay(false);
        }

        void endInput(const InputEvent &e) {
            if (e.repeat) {
                return;
            }
            if (countdown.afterEnd()) {
                // the card after the end: Play now, or Cancel (the Up next panel stays: Play next / Back)
                if (e.button == PadButton::Cross && countdown.playNow()) {
                    startNext("play now after the end of file");   // log reason i18n-exempt
                } else if (e.button == PadButton::Circle && countdown.cancel()) {
                    LOG_I("player", "next episode card after the end: cancelled");
                    showEndPanel();
                }
                return;
            }
            if (e.button == PadButton::Circle) {
                exit();
            } else if (e.button == PadButton::Cross) {
                if (isEpisode() && nextIdx >= 0) {
                    playNext();
                } else {
                    app.library().resetProgress(isEpisode() ? ContentType::Series : ContentType::Movie, item().id);
                    startItem(index, false);
                }
            }
        }

        void playNext() {
            if (nextIdx >= 0 && nextIdx < (int) queue.size()) {
                startItem(nextIdx, true);
            }
        }

        // ------------------------------------------------------------------ Continue Watching completion (45 s)
        // every frame: the last 45 s latch the episode as complete for Continue Watching (app/auto_next.h)
        void updateCompletion(double now) {
            if (!isEpisode() || item().localMedia) {
                return;
            }
            Playback &pb = app.playback();
            double duration = tracker.duration() > 0 ? tracker.duration() : pb.duration();
            switch (completion.update(tracker.position(), duration, tracker.started())) {
                case autonext::CompletionLatch::Change::Latched:
                    tracker.setCompleted(true);
                    tracker.record(now);   // watched from now on, whatever happens next
                    LOG_I("player", "episode %s complete for Continue Watching (%.1f s left)", item().id.c_str(),
                          duration - tracker.position());
                    persistUpNext("last 45 s");   // log reason i18n-exempt
                    app.saveLibraryInBackground();
                    break;
                case autonext::CompletionLatch::Change::Released:
                    tracker.setCompleted(false);   // seeked back: in progress again (the next record says so)
                    LOG_I("player", "episode %s in progress again (seeked back)", item().id.c_str());
                    break;
                default:
                    break;
            }
            if (completion.latched() && !upNextDone && nextIdx >= 0) {
                persistUpNext("next episode known");   // the catalog arrived after the latch; log reason i18n-exempt
                app.saveLibraryInBackground();
            }
        }

        // the episode is complete: the provider's next episode becomes the series' Continue Watching entry at 0 s
        // (once per item). Without a valid next episode nothing is created (final episode, offline and not
        // downloaded, unknown): the series leaves Continue Watching as before.
        void persistUpNext(const char *why) {
            if (upNextDone || !isEpisode() || item().localMedia || nextIdx < 0) {
                return;
            }
            upNextDone = true;
            const VodItem &n = queue[(size_t) nextIdx];
            std::shared_ptr<const SeriesInfo> info = screens::episodeCatalog(app, item());
            autonext::UpNext r = autonext::persistUpNext(app.library(), queue, index, nextIdx, info.get(),
                                                         clockx::unixNow());
            if (r == autonext::UpNext::Moved) {
                LOG_I("player", "continue watching -> next episode %s (S%02dE%02d) at 0 s (%s)", n.id.c_str(), n.season,
                      n.episode, why);
            } else {
                const HistoryEntry *cur = app.library().seriesContinueEntry(n.seriesId);
                LOG_I("player", "continue watching stays at %s (S%02dE%02d: later or in progress) (%s)",
                      cur ? cur->id.c_str() : "-", cur ? cur->season : 0, cur ? cur->episode : 0, why);
            }
        }

        // ------------------------------------------------------------------ next-episode countdown
        // once per item: the episode after this one (app/auto_next.h), from the catalog already in memory
        void resolveNext() {
            nextIdx = -1;
            if (!isEpisode()) {
                return;
            }
            std::shared_ptr<const SeriesInfo> info = screens::episodeCatalog(app, item());
            nextIdx = autonext::nextIndex(queue, index, info.get(), app.session().offline);
            // no catalog in memory (e.g. Continue Watching right after start): ask once, online only; the queue is
            // completed when it arrives (completeQueue). Another profile's download keeps its own queue.
            catalogWanted = false;
            const VodItem &cur = item();
            if (!info && !app.session().offline && (cur.profileId.empty() || cur.profileId == app.session().profile.id)) {
                catalogWanted = true;
                vodGen = app.vod().generation();
                if (catalogRequested != cur.seriesId) {
                    catalogRequested = cur.seriesId;
                    LOG_I("player", "series details not in memory: requesting them once for the next episode");
                    app.vod().requestSeriesInfo(app.session().profile, cur.seriesId);
                }
            }
            if (nextIdx >= 0) {
                const VodItem &n = queue[(size_t) nextIdx];
                nextText->setText(fmt::episodeCode(n.season, n.episode) + "  \xC2\xB7  " + episodeTitle(n));
                LOG_I("player", "next episode: %s (S%02dE%02d%s, %s)", n.id.c_str(), n.season, n.episode,
                      n.localPath.empty() ? "" : ", downloaded", info ? "catalog" : "queue order");
            } else {
                LOG_I("player", "next episode: none - no Next Episode card for this episode (%s)",
                      autonext::noNextReason(queue, index, info.get(), app.session().offline));
            }
            endApproachLogged = false;
            nextShownSeconds = -1;
            layoutNextActions();   // "Watch credits" again (after the end it reads "Cancel")
        }

        // the series' catalog arrived while this episode plays: the same queue the other entry points build
        void completeQueue() {
            if (!catalogWanted || finished || app.vod().generation() == vodGen) {
                return;
            }
            vodGen = app.vod().generation();
            if (!screens::episodeCatalog(app, item())) {
                return;   // not (yet) there; a failed request is not repeated
            }
            int position = 0;
            std::vector<VodItem> q = screens::seriesEpisodeQueue(app, item(), position);
            catalogWanted = false;
            if (position < 0 || position >= (int) q.size() || q[(size_t) position].id != item().id) {
                return;
            }
            queue = std::move(q);
            index = position;   // the current item is unchanged (the tracker keeps its own copy)
            LOG_I("player", "series details arrived: %d episodes in the queue", (int) queue.size());
            resolveNext();
            refresh(app.now());
        }

        bool autoPlay() const {
            return app.settings().get().autoPlayNextEpisode;
        }

        // i18n-exempt-begin: log values
        const char *durationSource() const {
            return app.playback().duration() > 0 ? "mpv" : tracker.duration() > 0 ? "provider hint" : "unknown";
        }

        // once per episode, ~48 s before the reported end: the state the card depends on (no URL, no credentials)
        void logEndApproach(const autonext::Frame &f, double position, double duration) {
            if (endApproachLogged || !isEpisode() || f.remaining < 0 || f.remaining > autonext::CARD_SHOW_REMAINING + 3) {
                return;
            }
            endApproachLogged = true;
            const char *card = nextIdx < 0 ? "none (no next episode)" : !f.running ? "held (playback not running normally)"
                               : f.autoPlay ? "due (auto-play)" : "due (manual, auto-play off)";
            LOG_I("player", "end approach: position %.2f of %.2f s (duration from %s), %.2f s left, state %d, recovery %s, "
                            "options panel %s, next %s, auto-play %s -> card %s", position, duration, durationSource(),
                  f.remaining, (int) app.playback().state(), statusName(recovery.status()), panelOpen ? "open" : "closed",
                  nextIdx >= 0 ? queue[(size_t) nextIdx].id.c_str() : "none", f.autoPlay ? "on" : "off", card);
        }

        // the end of file: how far from the reported duration it came, and what the card does
        void logEndOfFile(bool cardWasShown) {
            double duration = tracker.duration();
            double position = tracker.position();
            const char *card;
            if (nextIdx < 0) {
                card = "none: no next episode";
            } else if (countdown.phase() == autonext::Phase::Cancelled) {
                card = "none: watch credits was chosen (Up next panel)";
            } else if (cardWasShown) {
                card = autoPlay() ? "was shown, its count continues after the end" : "was shown, stays (manual)";
            } else {
                card = autoPlay() ? "shown after the end (the end came before the 45 s window), 7 s countdown"
                                  : "shown after the end (manual, auto-play off)";
            }
            LOG_I("player", "end of file at %.2f s, reported duration %.2f s (from %s, %.2f s short), next %s, auto-play "
                            "%s: card %s", position, duration, durationSource(), duration > 0 ? duration - position : -1.0,
                  nextIdx >= 0 ? queue[(size_t) nextIdx].id.c_str() : "none", autoPlay() ? "on" : "off", card);
        }
        // i18n-exempt-end

        // the Next Episode card's frame: true = start the next episode now
        bool updateCountdown() {
            Playback &pb = app.playback();
            autonext::Frame f;
            f.hasNext = nextIdx >= 0;
            f.autoPlay = autoPlay();
            f.running = !panelOpen && pb.started() && recovery.status() == stability::Status::Playing
                        && (pb.state() == PlaybackState::Playing || pb.state() == PlaybackState::Paused);
            f.paused = pb.state() == PlaybackState::Paused;
            f.now = app.now();
            double duration = tracker.duration() > 0 ? tracker.duration() : pb.duration();
            double position = pendingSeek ? seekTarget : pb.position();
            f.remaining = duration > 0 && position >= 0 ? std::max(0.0, duration - position) : -1;
            if (!finished) {
                logEndApproach(f, position, duration);
            }
            bool wasVisible = countdown.visible();
            if (countdown.update(f)) {
                LOG_I("player", "next episode card: countdown finished (%.1f s left in the file)", f.remaining);
                return true;
            }
            if (countdown.visible()) {
                if (!wasVisible) {
                    LOG_I("player", "next episode card shown: %.1f s left, %s", f.remaining,
                          f.autoPlay ? "7 s countdown" : "manual (auto-play off)");   // i18n-exempt: log value
                    showNextPanel(true);
                    refresh(app.now());
                }
                // auto-play OFF: a manual card - no seconds, no bar
                const bool counting = countdown.counting();
                nextCount->setVisibility(counting ? Visibility::Visible : Visibility::Hidden);
                nextTrack->setVisibility(counting ? Visibility::Visible : Visibility::Hidden);
                if (counting && countdown.seconds() != nextShownSeconds) {
                    nextShownSeconds = countdown.seconds();   // the number changes once per second, not per frame
                    nextCount->setText(tr("player.next_seconds", {std::to_string(nextShownSeconds)}));
                }
                // the thin bar fills smoothly over the card's 7 seconds; the card slides in from the right
                namespace P = layout::player;
                float w = std::round((P::NEXT_W - 2 * P::NEXT_PAD) * countdown.fraction());
                nextFill->setSize(std::max(P::NEXT_BAR_H, w), P::NEXT_BAR_H);
                double t = std::min(1.0, (f.now - nextShownAt) / P::NEXT_SLIDE_SECONDS);
                float ease = (float) (1 - (1 - t) * (1 - t) * (1 - t));
                nextPanel->setPosition(std::round(P::NEXT_X + P::NEXT_SLIDE * (1 - ease)), P::NEXT_Y);
            } else if (wasVisible) {
                showNextPanel(false);   // seeked back / stopped running normally: the normal controls are back
                refresh(app.now());
            }
            return false;
        }

        // the countdown ran out or Cross: the current episode is complete, then the next one starts
        void advance(const char *why) {
            if (nextIdx < 0) {
                return;
            }
            finished = true;
            tracker.record(app.now(), -1, true);   // completed, as at the natural end (leaves Continue Watching)
            persistUpNext(why);
            app.saveLibrary();
            showNextPanel(false);
            startNext(why);
        }

        // the one transition: the player's normal item start (also used by L1 / R1 and Series Detail's choice)
        void startNext(const char *why) {
            int target = nextIdx;
            if (target < 0 || target >= (int) queue.size()) {
                return;
            }
            LOG_I("player", "next episode (%s): %s -> %s", why, item().id.c_str(), queue[(size_t) target].id.c_str());
            if (panelOpen) {
                closePanel();
            }
            startItem(target, true);
        }

        void showNextPanel(bool shown) {
            if (!shown) {
                nextShownSeconds = -1;
            } else if (!nextPanel->isVisible()) {
                nextShownAt = app.now();   // slide in from here
                nextPanel->setPosition(layout::player::NEXT_X + layout::player::NEXT_SLIDE, layout::player::NEXT_Y);
            }
            nextPanel->setVisibility(shown ? Visibility::Visible : Visibility::Hidden);
        }

        // The Next Episode card, bottom right (layout::player NEXT_*), on a near-black translucent surface:
        //   NEXT EPISODE                         7s
        //   S02E05 · Episode title
        //   [(X) Play now]   (O) Watch credits
        //   ━━━━━━━━━━━━━━━━━━━━━━━──────────────────   (the countdown: a thin accent bar)
        // Text only (no artwork is fetched for it); the glyphs follow the device in use.
        void buildNextPanel() {
            namespace P = layout::player;
            const float inner = P::NEXT_W - 2 * P::NEXT_PAD;
            nextPanel = ui::box(this, FloatRect(P::NEXT_X, P::NEXT_Y, P::NEXT_W, P::NEXT_H), theme::overlayPanel(),
                                theme::RADIUS);
            nextPanel->setOutlineColor(theme::divider());
            nextPanel->setOutlineThickness(1);
            ui::label(nextPanel, tr("player.next_eyebrow"), theme::CAPTION, P::NEXT_PAD, 16, ui::Weight::SemiBold,
                      theme::textSecondary());
            nextCount = ui::label(nextPanel, "", theme::CAPTION, 0, 16, ui::Weight::SemiBold, theme::textMuted());
            nextCount->setAlign(ui::Align::Right, P::NEXT_W - P::NEXT_PAD);
            nextText = ui::label(nextPanel, "", theme::BODY, P::NEXT_PAD, 46, ui::Weight::SemiBold, theme::textPrimary());
            nextText->setMaxWidth(inner);
            nextText->setMaxLines(1);
            nextActions = new RectangleShape(FloatRect(P::NEXT_PAD, 90, inner, P::NEXT_BUTTON_H));
            nextActions->setFillColor(theme::none());
            nextPanel->add(nextActions);
            layoutNextActions();
            nextTrack = ui::box(nextPanel, FloatRect(P::NEXT_PAD, P::NEXT_H - 12 - P::NEXT_BAR_H, inner, P::NEXT_BAR_H),
                                theme::progressTrack(), P::NEXT_BAR_H / 2);
            nextFill = ui::box(nextTrack, FloatRect(0, 0, P::NEXT_BAR_H, P::NEXT_BAR_H), theme::accent(), P::NEXT_BAR_H / 2);
            nextPanel->setVisibility(Visibility::Hidden);
        }

        // [glyph Play now] as the card's primary pill, then [glyph] Watch credits, for the device in use
        void layoutNextActions() {
            namespace P = layout::player;
            for (auto *c: nextActions->getChilds()) {
                nextActions->remove(c);
                delete c;
            }
            const auto device = actions::tracker().active();
            const float h = P::NEXT_BUTTON_H;
            const float glyph = 30;
            auto *pill = ui::box(nextActions, FloatRect(0, 0, 10, h), theme::accent(), h / 2);
            float x = 14;
            float gw = ui::actionGlyph(pill, A::PlayNow, x, (h - glyph) / 2, glyph, device);
            x += gw + 10;
            auto *play = ui::label(pill, tr("player.next_play_now"), theme::LABEL, x, ui::Label::centerOffset(theme::LABEL, h),
                                   ui::Weight::SemiBold, theme::onAccent());
            x += play->width() + 18;
            pill->setSize(x, h);
            x += 22;
            float cw = ui::actionGlyph(nextActions, A::WatchCredits, x, (h - glyph) / 2, glyph, device);
            x += cw + 10;
            ui::label(nextActions, tr(countdown.afterEnd() ? "common.cancel" : "player.watch_credits"), theme::LABEL, x,
                      ui::Label::centerOffset(theme::LABEL, h), ui::Weight::Regular, theme::textSecondary());
        }

        // a row of hints centred in a panel (end of playback / retry)
        void setCentredHints(ui::HintBar *bar, const screens::Hints &h) {
            bar->setHints(h);
            const float w = bar->getSize().x;
            bar->setPosition(std::round((centre->getSize().x - std::min(w, bar->contentWidth())) / 2), bar->getPosition().y);
        }

        void onInputDeviceChanged() override {
            layoutNextActions();
            for (ui::HintBar *b: {endHints, centreHints}) {
                b->setPosition(std::round((centre->getSize().x - std::min(b->getSize().x, b->contentWidth())) / 2),
                               b->getPosition().y);
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
                app.saveLibraryInBackground();   // every DISK_EVERY s while playing: no fsync on the render thread
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
            namespace P = layout::player;
            // top: overlay behind the title, then an eased fade into the picture (no band edge)
            top = ui::box(this, FloatRect(0, 0, theme::SCREEN_W, P::TOP_FADE_H), theme::none(), 0);
            screens::hudShade(top, P::TOP_FADE_H, 100, true);
            title = ui::label(top, "", theme::TITLE, theme::SAFE_X, P::TITLE_Y, ui::Weight::SemiBold);
            title->setMaxWidth(1400);
            subtitle = ui::label(top, "", theme::BODY, theme::SAFE_X, P::SUBTITLE_Y, ui::Weight::Regular,
                                 theme::textSecondary());
            subtitle->setMaxWidth(1400);
            clock = ui::label(top, "", theme::HEADING, 0, P::TITLE_Y + 4, ui::Weight::SemiBold);
            clock->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);

            // bottom: progress, time and track info, hints, on a shade that fades up into the picture
            const float bh = theme::SCREEN_H - P::BOTTOM_Y;
            bottom = ui::box(this, FloatRect(0, P::BOTTOM_Y, theme::SCREEN_W, bh), theme::none(), 0);
            screens::hudShade(bottom, bh, 180, false);
            const float barW = theme::SCREEN_W - 2 * theme::SAFE_X;
            barTrack = ui::box(bottom, FloatRect(theme::SAFE_X, P::BAR_Y - P::BOTTOM_Y, barW, P::BAR_H),
                               theme::progressTrack(), P::BAR_H / 2);
            barFill = ui::box(barTrack, FloatRect(0, 0, P::BAR_H, P::BAR_H), theme::accent(), P::BAR_H / 2);
            barThumb = new CircleShape(10);
            barThumb->setPointCount(20);
            barThumb->setFillColor(theme::textStrong());
            barThumb->setOrigin(Origin::Center);
            barTrack->add(barThumb);
            timeLabel = ui::label(bottom, "", theme::BODY, theme::SAFE_X, P::TIME_Y - P::BOTTOM_Y, ui::Weight::SemiBold);
            chips = ui::label(bottom, "", theme::LABEL, 0, P::TIME_Y - P::BOTTOM_Y + 4, ui::Weight::Regular,
                              theme::textSecondary());
            chips->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);
            chips->setMaxWidth(1100);
            hints = new ui::HintBar();
            hints->setOwner(this);
            // keyboard shortcuts that work without being shown in the HUD (the controller has them too)
            hints->setShortcuts({A::Info, A::Favorite, A::PreviousEpisode, A::SeekMinute});
            hints->setPosition(theme::SAFE_X, layout::HINTS_Y - P::BOTTOM_Y);
            bottom->add(hints);

            pausePill = ui::box(this, FloatRect((theme::SCREEN_W - 260) / 2, 460, 260, 90), theme::overlayPanel(), 45);
            auto *pl = ui::label(pausePill, tr("player.paused"), theme::HEADING, 0, ui::Label::centerOffset(theme::HEADING, 90),
                                 ui::Weight::SemiBold);
            pl->setAlign(ui::Align::Center, 260);
            pausePill->setVisibility(Visibility::Hidden);

            pill = ui::box(this, FloatRect((theme::SCREEN_W - 460) / 2, 700, 460, 72), theme::overlayPanel(), 36);
            pillText = ui::label(pill, tr("player.buffering"), theme::BODY, 84, ui::Label::centerOffset(theme::BODY, 72),
                                 ui::Weight::SemiBold);
            pillSpinner = new ui::Spinner(12);
            pillSpinner->setPosition(30, (72 - 12) / 2);
            pill->add(pillSpinner);
            pill->setVisibility(Visibility::Hidden);
        }

        void buildCentre() {
            const float w = 1160;
            centre = ui::box(this, FloatRect((theme::SCREEN_W - w) / 2, 380, w, 300), theme::overlayPanel(),
                             theme::RADIUS);
            centreTitle = ui::label(centre, "", theme::HEADING, 0, 52, ui::Weight::SemiBold);
            centreTitle->setAlign(ui::Align::Center, w);
            centreTitle->setMaxWidth(w - 80);
            centreText = ui::label(centre, "", theme::BODY, 0, 118, ui::Weight::Regular, theme::textSecondary());
            centreText->setAlign(ui::Align::Center, w);
            centreText->setMaxWidth(w - 80);
            centreText->setMaxLines(2);
            centreHints = new ui::HintBar();
            centreHints->setOwner(this);
            centreHints->setPosition(0, 206);
            centreHints->setSize(w, layout::HINTS_H);
            centre->add(centreHints);
            spinner = new ui::Spinner(18);
            spinner->setPosition((w - 18 * 3.6f) / 2, 230);
            centre->add(spinner);

            endPanel = ui::box(this, FloatRect((theme::SCREEN_W - w) / 2, 380, w, 300), theme::overlayPanel(),
                               theme::RADIUS);
            endTitle = ui::label(endPanel, "", theme::LABEL, 0, 50, ui::Weight::SemiBold, theme::accentText());
            endTitle->setAlign(ui::Align::Center, w);
            endText = ui::label(endPanel, "", theme::HEADING, 0, 100, ui::Weight::SemiBold);
            endText->setAlign(ui::Align::Center, w);
            endText->setMaxWidth(w - 80);
            endHints = new ui::HintBar();
            endHints->setOwner(this);
            endHints->setPosition(0, 206);
            endHints->setSize(w, layout::HINTS_H);
            endPanel->add(endHints);
            endPanel->setVisibility(Visibility::Hidden);
        }

        void buildInfo() {
            info = ui::box(this, FloatRect(theme::SAFE_X, 220, 660, 560), theme::overlayPanel(), theme::RADIUS);
            ui::label(info, tr("info.title"), theme::HEADING, 36, 30, ui::Weight::SemiBold);
            infoText = ui::label(info, "", theme::LABEL, 36, 96, ui::Weight::Regular, theme::textSecondary());
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
            std::string copy = tr(it.localMedia ? "player.local_file" : "player.offline_copy");
            std::string source = it.localPath.empty() ? "" : "   \xC2\xB7   " + copy;
            if (isEpisode()) {
                title->setText(it.seriesName);
                subtitle->setText(fmt::episodeCode(it.season, it.episode) + "  \xE2\x80\x94  " + episodeTitle(it) + source);
            } else {
                title->setText(it.title);
                std::string year = it.year > 0 ? std::to_string(it.year) : "";
                subtitle->setText(!year.empty() ? year + source : it.localPath.empty() ? "" : copy);
            }
            clock->setText(clockx::localTime());

            double duration = tracker.duration() > 0 ? tracker.duration() : pb.duration();
            double position = pendingSeek ? seekTarget : (pb.started() ? pb.position() : tracker.position());
            float frac = (float) progress::fraction(position, duration);
            float w = barTrack->getSize().x;
            barFill->setSize(std::max(layout::player::BAR_H, std::round(w * frac)), layout::player::BAR_H);
            barThumb->setPosition(std::round(w * frac), layout::player::BAR_H / 2);
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
            // the everyday controls only (L2 / R2 still seek a minute)
            // (Triangle still shows the technical info; it is also in Options > Technical info)
            // while the next-episode countdown is shown, Cross / Circle are its Play now / Cancel
            const bool counting = countdown.visible();
            screens::Hints h;
            if (counting) {
                h.push_back({A::PlayNow, tr("player.next_play_now")});
            } else {
                h.push_back({A::PlayPause, tr(pb.state() == PlaybackState::Paused ? "common.play" : "player.pause")});
            }
            h.push_back({A::Seek, tr("player.seek_10")});
            h.push_back({A::Options, tr("player.options")});
            if (isEpisode()) {
                h.push_back({A::NextEpisode, tr("player.next_episode")});
            }
            h.push_back(counting ? ui::Hint{A::WatchCredits, tr(countdown.afterEnd() ? "common.cancel" : "player.watch_credits")}
                                 : ui::Hint{A::Back, tr("common.back")});
            hints->setHints(h);

            stability::Status rs = recovery.status();
            pausePill->setVisibility(pb.state() == PlaybackState::Paused && !panelOpen ? Visibility::Visible
                                                                                       : Visibility::Hidden);
            bool centreShown = !finished && (rs == stability::Status::Opening || rs == stability::Status::Reconnecting
                                             || rs == stability::Status::Refused || rs == stability::Status::Failed);
            centre->setVisibility(centreShown ? Visibility::Visible : Visibility::Hidden);
            bool spinning = centreShown && (rs == stability::Status::Opening || rs == stability::Status::Reconnecting);
            spinner->setVisibility(spinning ? Visibility::Visible : Visibility::Hidden);
            centreHints->setVisibility(spinning ? Visibility::Hidden : Visibility::Visible);
            centreTitle->setColor(rs == stability::Status::Failed ? theme::danger() : theme::textPrimary());
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
                setCentredHints(centreHints, {{A::Confirm, tr("player.retry_now")}, {A::Back, tr("common.cancel")}});
            } else if (rs == stability::Status::Failed) {
                bool stopped = pb.state() == PlaybackState::Error;
                centreTitle->setText(stopped && !pb.errorMessage().empty() ? pb.errorMessage() : tr("player.title_unavailable"));
                centreText->setText(recovery.attempts() > 0 ? tr("player.gave_up", {std::to_string(recovery.attempts())}) : "");
                setCentredHints(centreHints, {{A::Confirm, tr("player.retry")}, {A::Back, tr("common.back")}});
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
                        {"info.source", tr(it.localMedia ? "info.source_local" : it.localPath.empty() ? "info.source_stream" : "info.source_file")},
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
            SubtitleStyleMenu,
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
            MENU_VIDEO = 3,
            MENU_SUBTITLE_STYLE = 4
        };

        struct PanelItem {
            Row row;
            int trackId = 0;
        };

        void buildPanel() {
            namespace P = layout::player;
            panel = ui::box(this, FloatRect(P::OPTIONS_X, 0, P::OPTIONS_W, theme::SCREEN_H), theme::panel(), 0);
            // a soft left edge instead of a cut into the picture
            ui::fade(panel, FloatRect(-P::OPTIONS_EDGE_W, 0, P::OPTIONS_EDGE_W, theme::SCREEN_H), theme::panel(),
                     ui::Edge::Right);
            panelTitle = ui::label(panel, "", theme::TITLE, P::OPTIONS_PAD, 70, ui::Weight::SemiBold);
            panelTitle->setMaxWidth(P::OPTIONS_LIST_W);
            // the rows end at the safe edge (x 1824), chevrons inside them
            panelList = new ui::ListView(FloatRect(P::OPTIONS_PAD, 170, P::OPTIONS_LIST_W + ui::ScrollBar::WIDTH
                                                                         + ui::ScrollBar::GAP, 760),
                                         P::OPTIONS_ROW_H, 4, this);
            panel->add(panelList);
            auto *ph = new ui::HintBar();
            ph->setOwner(this);
            ph->setPosition(P::OPTIONS_PAD, layout::HINTS_Y);
            ph->setHints({{A::Confirm, tr("common.select")}, {A::Change, tr("settings.hint_change")},
                          {A::Back, tr("common.back")}});
            panel->add(ph);
            panel->setVisibility(Visibility::Hidden);
        }

        int count() override { return (int) panelItems.size(); }

        C2DObject *createRow(float w, float h) override {
            PanelRow r;
            r.bg = new ui::RowFrame(w, h, -1);
            r.check = ui::label(r.bg, "", theme::BODY, 20, ui::Label::centerOffset(theme::BODY, h) - 12,
                                ui::Weight::SemiBold, theme::accentText());
            r.name = ui::label(r.bg, "", theme::BODY, 56, ui::Label::centerOffset(theme::BODY, h) - 12);
            r.name->setMaxWidth(w - 56 - 52);
            r.detail = ui::label(r.bg, "", theme::CAPTION, 56, ui::Label::centerOffset(theme::BODY, h) + 22,
                                 ui::Weight::Regular, theme::textSecondary());
            r.detail->setMaxWidth(w - 56 - 52);
            // rows that open a menu end with a chevron, 20 px inside the row
            r.chevron = ui::label(r.bg, "\xE2\x80\xBA", theme::HEADING, 0, ui::Label::centerOffset(theme::HEADING, h) - 2,
                                  ui::Weight::SemiBold, theme::textMuted());
            r.chevron->setAlign(ui::Align::Right, w - 20);
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
                case Row::SubtitleStyleMenu:
                    name = tr("options.subtitle_style");
                    detail = tr(SIZE_KEYS[std::min(std::max(s.subtitleSize, 0), 2)]) + "  \xC2\xB7  "
                             + tr(POSITION_KEYS[s.subtitlePosition == 1 ? 1 : 0]) + "  \xC2\xB7  "
                             + tr("subtitle.shadow") + " " + tr(s.subtitleShadow ? "common.on" : "common.off");
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
                bool opensMenu = pi.row == Row::AudioMenu || pi.row == Row::SubtitleMenu || pi.row == Row::VideoMenu
                                 || pi.row == Row::SubtitleStyleMenu;
                r.chevron->setVisibility(opensMenu ? Visibility::Visible : Visibility::Hidden);
                r.chevron->setColor(focused ? theme::textPrimary() : theme::textMuted());
                r.name->setText(name);
                r.detail->setText(detail);
                r.name->setColor(focused ? theme::textStrong() : theme::textPrimary());
                r.name->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                r.detail->setColor(focused ? theme::textSecondary() : theme::textMuted());
                r.bg->setState(focused);
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
                // five groups: what plays (audio, subtitles, picture), how subtitles look, technical details
                for (Row r: {Row::AudioMenu, Row::SubtitleMenu, Row::VideoMenu, Row::SubtitleStyleMenu, Row::TechInfo}) {
                    panelItems.push_back({r, 0});
                }
            } else if (level == MENU_SUBTITLE_STYLE) {
                panelTitle->setText(tr("options.subtitle_style"));
                for (Row r: {Row::SubtitleSize, Row::SubtitlePosition, Row::SubtitleShadow}) {
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
                        showMenu(MENU_MAIN, panelLevel == MENU_AUDIO ? 0 : panelLevel == MENU_SUBTITLES ? 1
                                            : panelLevel == MENU_VIDEO ? 2 : 3);
                    } else {
                        closePanel();
                    }
                    return;
                case PadButton::Left:
                case PadButton::Right:
                case PadButton::Cross:
                    if (e.button == PadButton::Cross && !e.repeat && openPicker(panelItems[(size_t) panelList->selected()].row)) {
                        return;   // every value of the row in the shared choice picker (Left / Right still step)
                    }
                    if (!e.repeat || e.button != PadButton::Cross) {
                        activate(panelItems[(size_t) panelList->selected()], e.button == PadButton::Left ? -1 : 1,
                                 e.button == PadButton::Cross);
                    }
                    return;
                default:
                    return;
            }
        }

        // rows with 3+ values open the shared choice picker over the darkened, still playing video; false: not one
        bool openPicker(Row row) {
            std::vector<std::string> names;
            int current = 0;
            std::string title;
            switch (row) {
                case Row::Aspect:
                    title = tr("geometry.aspect");
                    for (int i = 0; i < display::ASPECT_COUNT; i++) {
                        names.push_back(screens::aspectName((display::Aspect) i));
                    }
                    current = (int) geometry.aspect;
                    break;
                case Row::Crop:
                    title = tr("geometry.crop");
                    for (int i = 0; i < display::CROP_COUNT; i++) {
                        names.push_back(screens::cropName((display::Crop) i));
                    }
                    current = (int) geometry.crop;
                    break;
                case Row::Zoom: {
                    title = tr("geometry.zoom");
                    const auto &steps = display::zoomSteps();
                    current = -1;
                    for (size_t i = 0; i < steps.size(); i++) {
                        names.push_back(screens::zoomName(steps[i]));
                        if (steps[i] == geometry.zoom) {
                            current = (int) i;
                        }
                    }
                    break;
                }
                case Row::PositionX:
                case Row::PositionY: {
                    const bool horizontal = row == Row::PositionX;
                    title = tr(horizontal ? "geometry.pos_x" : "geometry.pos_y");
                    for (int p = -display::POSITION_STEPS; p <= display::POSITION_STEPS; p++) {
                        names.push_back(screens::positionName(p, horizontal));
                    }
                    current = (horizontal ? geometry.posX : geometry.posY) + display::POSITION_STEPS;
                    break;
                }
                case Row::SubtitleSize:
                    title = tr("subtitle.size");
                    for (const char *k: SIZE_KEYS) {
                        names.push_back(tr(k));
                    }
                    current = app.settings().get().subtitleSize;
                    break;
                default:
                    return false;
            }
            app.push(screens::makeChoicePicker(app, title, names, current, whileAlive([this, row](int c) {
                if (c >= 0 && panelOpen) {
                    applyPicked(row, c);
                }
            })));
            return true;
        }

        // a value chosen in the picker: applied at once like a Left / Right step (no reload)
        void applyPicked(Row row, int c) {
            Settings &s = app.settings().get();
            switch (row) {
                case Row::Aspect:
                    geometry.aspect = (display::Aspect) std::min(std::max(c, 0), display::ASPECT_COUNT - 1);
                    applyGeometry();
                    break;
                case Row::Crop:
                    geometry.crop = (display::Crop) std::min(std::max(c, 0), display::CROP_COUNT - 1);
                    applyGeometry();
                    break;
                case Row::Zoom: {
                    const auto &steps = display::zoomSteps();
                    geometry.zoom = steps[(size_t) std::min(std::max(c, 0), (int) steps.size() - 1)];
                    applyGeometry();
                    break;
                }
                case Row::PositionX:
                case Row::PositionY: {
                    int p = std::min(std::max(c - display::POSITION_STEPS, -display::POSITION_STEPS), display::POSITION_STEPS);
                    (row == Row::PositionX ? geometry.posX : geometry.posY) = p;
                    applyGeometry();
                    break;
                }
                case Row::SubtitleSize: {
                    s.subtitleSize = std::min(2, std::max(0, c));
                    app.playback().applyOptions(tracks::appearanceOptions(s.subtitleSize, s.subtitlePosition,
                                                                          s.subtitleShadow));
                    std::string err;
                    if (!app.settings().save(&err)) {
                        LOG_E("settings", "save failed: %s", err.c_str());
                    }
                    break;
                }
                default:
                    return;
            }
            panelList->reload();
            refresh(app.now());
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
                case Row::SubtitleStyleMenu:
                    if (delta > 0) {
                        showMenu(MENU_SUBTITLE_STYLE);
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
            ui::RowFrame *bg;
            ui::Label *check;
            ui::Label *name;
            ui::Label *detail;
            ui::Label *chevron;
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
        autonext::Countdown countdown;
        autonext::CompletionLatch completion;   // the last 45 s: complete for Continue Watching
        bool upNextDone = false;                // the next episode was made the series' Continue Watching entry
        double nextShownAt = 0;                 // the card's slide-in start
        RectangleShape *nextActions = nullptr;
        int nextIdx = -1;                  // queue index of the next episode (-1: none / not an episode)
        bool catalogWanted = false;        // waiting for the series' catalog to complete the queue
        unsigned vodGen = 0;
        std::string catalogRequested;      // asked once per series while the player is open
        CancelToken linkToken;             // Stalker: the create_link request of the current open
        double awaitingInfoStart = -1;     // Stalker: open() waits for the episode list (resume position)
        unsigned awaitingInfoGen = 0;
        std::string awaitingInfoFor;
        int nextShownSeconds = -1;
        RectangleShape *nextPanel = nullptr;
        ui::Label *nextText = nullptr;
        ui::Label *nextCount = nullptr;
        RectangleShape *nextFill = nullptr;
        RectangleShape *nextTrack = nullptr;
        bool endApproachLogged = false;    // the "end approach" diagnostics line, once per episode
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
        ui::HintBar *centreHints;
        ui::Label *endTitle;
        ui::Label *endText;
        ui::HintBar *endHints;
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
