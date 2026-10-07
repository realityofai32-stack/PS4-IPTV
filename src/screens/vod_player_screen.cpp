// Movie / episode playback on the proven pPlay backend (player/playback + pplay VideoTexture).
//
// Same mpv instance, options and recovery logic as Live TV, plus what VOD needs:
//   - resume: mpv's per-file "start" option from the saved position
//   - seeking: Left/Right 10 s, L2/R2 1 min; presses accumulate and one keyframe seek is sent when they stop
//   - tracks: picked automatically when the file loads (Settings: audio language, subtitle mode/language),
//     switched at runtime from the Options panel through mpv's aid / sid (no reload)
//   - progress: kept in memory every 10 s, written to disk every 30 s, on pause, on exit and at the end
//   - episodes: L1/R1 previous/next, an "Up next" panel at the end (auto-play only when enabled in Settings)

#include <cctype>
#include <cmath>

#include "common.h"
#include "../core/format.h"
#include "../iptv/xtream.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../player/pplay/video_texture.h"

using namespace c2d;
using namespace iptv;
using screens::VodItem;

namespace {

    const double OVERLAY_TIMEOUT = 5.0;
    const double SEEK_COMMIT_DELAY = 0.45;    // seconds after the last seek press
    const double PROGRESS_MEMORY_EVERY = 10;
    const double PROGRESS_DISK_EVERY = 30;
    const double END_MARGIN = 30;             // an end of file this close to the duration is the real end
    const double AUTOPLAY_COUNTDOWN = 10;
    const float PANEL_W = 680;

    const char *SIZE_NAMES[] = {"Small", "Medium", "Large"};
    const char *POSITION_NAMES[] = {"Bottom", "Raised"};

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

    class VodPlayerScreen : public Screen, public ui::ListView::Adapter {
    public:
        VodPlayerScreen(App &a, std::vector<VodItem> items, int start, bool resumeFirst,
                        std::function<void(const std::string &)> exitCallback)
                : Screen(a), queue(std::move(items)), index(start), resumeOnStart(resumeFirst),
                  onExit(std::move(exitCallback)) {
            setFillColor(Color::Black);
            video = new VideoTexture(app.playback().backend(), {theme::SCREEN_W, theme::SCREEN_H});
            add(video);
            buildOverlay();
            buildCentre();
            buildPanel();
            buildInfo();
        }

        ~VodPlayerScreen() override {
            app.playback().stop();
        }

        const char *name() const override { return "vod-player"; }

        bool animating() const override { return true; }

        void onEnter() override {
            startItem(index, resumeOnStart);
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
                LOG_I("player", "seek to %.0fs", seekTarget);
                pb.seekTo(seekTarget);
                lastKnownPosition = seekTarget;
            }
            if (pb.started() && pb.state() == PlaybackState::Playing && !pendingSeek) {
                lastKnownPosition = pb.position();
                if (pb.duration() > 0) {
                    knownDuration = pb.duration();
                }
            }
            if (now - lastProgressMemory >= PROGRESS_MEMORY_EVERY && pb.started()) {
                saveProgress(now - lastProgressDisk >= PROGRESS_DISK_EVERY);
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
                app.toast("These subtitles cannot be displayed on PS4: subtitles turned off", ToastKind::Error);
            }
            if (finished && autoplayAt > 0 && now >= autoplayAt) {
                autoplayAt = 0;
                playNext();
                return;
            }
            bool paused = pb.state() == PlaybackState::Paused;
            if (overlayVisible && !paused && !pendingSeek && !panelOpen && !finished && now - lastInput > OVERLAY_TIMEOUT
                && recovery.status() == stability::Status::Playing) {
                setOverlay(false);
            }
            if (now - lastRefresh >= 0.25) {
                lastRefresh = now;
                refresh(now);
            }
        }

        // ------------------------------------------------------------------ input
        void handleInput(const InputEvent &e) override {
            lastInput = app.now();
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
                        open(lastKnownPosition);
                    } else if (pb.state() == PlaybackState::Playing || pb.state() == PlaybackState::Paused) {
                        bool pause = pb.state() == PlaybackState::Playing;
                        pb.setPaused(pause);
                        if (pause) {
                            saveProgress(true);
                        }
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
                            saveProgress(true);
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
                        app.toast(on ? "Added to favorites" : "Removed from favorites");
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
            finished = false;
            autoplayAt = 0;
            endPanel->setVisibility(Visibility::Hidden);
            knownDuration = item().durationHint;
            lastKnownPosition = 0;
            subtitleWarningShown = false;
            languagesRecorded = false;
            manualAudio = -1;
            manualSub = -2;
            const Settings &s = app.settings().get();
            preset = s.stability;
            recovery.begin(stability::policy(preset), s.retryOnStall, false, app.now());
            double start = 0;
            const HistoryEntry *p = app.library().progressOf(isEpisode() ? ContentType::Series : ContentType::Movie,
                                                             item().id);
            if (resume && s.resumeVod && p && progress::canResume(p->position, p->duration, p->watched)) {
                start = progress::resumeFrom(p->position);
            }
            LOG_I("player", "%s %s (%s), %s, stability %s", isEpisode() ? "episode" : "movie", item().id.c_str(),
                  item().extension.c_str(), start > 0 ? diag::format("resume at %.0fs", start).c_str() : "from the start",
                  stabilityName(preset));
            lastKnownPosition = start;
            lastProgressMemory = app.now();
            lastProgressDisk = app.now();
            open(start);
            setOverlay(true);
            refresh(app.now());
        }

        void open(double start) {
            Playback &pb = app.playback();
            const Settings &s = app.settings().get();
            pb.applyOptions(stability::mpvOptions(preset));
            pb.applyOptions(tracks::appearanceOptions(s.subtitleSize, s.subtitlePosition, s.subtitleShadow));
            const VodItem &it = item();
            std::string url = isEpisode() ? xtream::seriesUrl(app.session().profile, it.id, it.extension)
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
                    double d = knownDuration;
                    if (pb.started() && (d <= 0 || lastKnownPosition >= d - END_MARGIN)) {
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
                LOG_I("player", "reconnecting at %.0fs (attempt %d/%d)", lastKnownPosition, recovery.attempts(),
                      recovery.maxAttempts());
                open(lastKnownPosition > 5 ? lastKnownPosition - 2 : 0);
            }
        }

        void reachedEnd() {
            finished = true;
            Playback &pb = app.playback();
            if (knownDuration > 0) {
                lastKnownPosition = knownDuration;
            }
            saveProgress(true, true);
            LOG_I("player", "%s finished", item().id.c_str());
            pb.stop();
            bool next = isEpisode() && index + 1 < (int) queue.size();
            endTitle->setText(next ? "Up next" : isEpisode() ? "You finished the last episode" : "The end");
            if (next) {
                const VodItem &n = queue[(size_t) index + 1];
                endText->setText(fmt::episodeCode(n.season, n.episode) + "  "
                                 + (n.title.empty() ? "Episode " + std::to_string(n.episode) : n.title));
                bool autoplay = app.settings().get().autoPlayNextEpisode;
                autoplayAt = autoplay ? app.now() + AUTOPLAY_COUNTDOWN : 0;
                endHint->setText(autoplay ? "X  Play now          Circle  Back" : "X  Play next          Circle  Back");
            } else {
                endText->setText(isEpisode() ? item().seriesName : item().title);
                endHint->setText("X  Watch again          Circle  Back");
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
                saveProgress(true);
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
            double max = knownDuration > 0 ? knownDuration - 3 : base + seconds;
            seekTarget = std::max(0.0, std::min(base + seconds, max));
            pendingSeek = true;
            seekCommitAt = app.now() + SEEK_COMMIT_DELAY;
            setOverlay(true);
            refresh(app.now());
        }

        // progress entry of the current item; toDisk: also write history.json
        void saveProgress(bool toDisk, bool atEnd = false) {
            lastProgressMemory = app.now();
            const VodItem &it = item();
            double duration = knownDuration > 0 ? knownDuration : app.playback().duration();
            double position = atEnd ? duration : lastKnownPosition;
            if (position <= 1 && !atEnd) {
                return;   // never started: nothing worth remembering
            }
            HistoryEntry h;
            h.type = isEpisode() ? ContentType::Series : ContentType::Movie;
            h.id = it.id;
            h.name = it.title.empty() && isEpisode() ? "Episode " + std::to_string(it.episode) : it.title;
            h.icon = it.image;
            h.extension = it.extension;
            h.seriesId = it.seriesId;
            h.seriesName = it.seriesName;
            h.season = it.season;
            h.episode = it.episode;
            h.position = position;
            h.duration = duration;
            h.watched = atEnd || progress::isWatched(position, duration);
            h.watchedAt = clockx::unixNow();
            app.library().updateProgress(h);
            if (toDisk) {
                lastProgressDisk = app.now();
                app.saveLibrary();
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
            auto *pl = ui::label(pausePill, "Paused", theme::HEADING, 0, ui::Label::centerOffset(theme::HEADING, 90),
                                 ui::Weight::SemiBold);
            pl->setAlign(ui::Align::Center, 260);
            pausePill->setVisibility(Visibility::Hidden);

            pill = ui::box(this, FloatRect((theme::SCREEN_W - 460) / 2, 700, 460, 72), Color(12, 16, 22, 210), 36);
            pillText = ui::label(pill, "Buffering" "\xE2\x80\xA6", theme::BODY, 84, ui::Label::centerOffset(theme::BODY, 72),
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
            ui::label(info, "Stream information", theme::HEADING, 36, 30, ui::Weight::SemiBold);
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
                s += "Audio: " + tracks::label(*a, a->id);
            }
            if (subCount > 0) {
                s += std::string(s.empty() ? "" : "   \xC2\xB7   ") + "Subtitles: " + (sub ? tracks::label(*sub, sub->id) : "Off");
            }
            return s;
        }

        void refresh(double now) {
            Playback &pb = app.playback();
            const VodItem &it = item();
            const StreamInfo &si = pb.info();
            if (isEpisode()) {
                title->setText(it.seriesName);
                subtitle->setText(fmt::episodeCode(it.season, it.episode) + "  \xE2\x80\x94  "
                                  + (it.title.empty() ? "Episode " + std::to_string(it.episode) : it.title));
            } else {
                title->setText(it.title);
                subtitle->setText(it.year > 0 ? std::to_string(it.year) : "");
            }
            clock->setText(clockx::localTime());

            double duration = knownDuration > 0 ? knownDuration : pb.duration();
            double position = pendingSeek ? seekTarget : (pb.started() ? pb.position() : lastKnownPosition);
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
                    {ui::Glyph::Cross, pb.state() == PlaybackState::Paused ? "Play" : "Pause"},
                    {ui::Glyph::DPad, "Seek 10 s"}, {ui::Glyph::L2, ""}, {ui::Glyph::R2, "1 min"},
                    {ui::Glyph::Options, "Audio & subtitles"}, {ui::Glyph::Triangle, "Info"}};
            if (isEpisode()) {
                h.push_back({ui::Glyph::R1, "Next episode"});
            }
            h.push_back({ui::Glyph::Circle, "Back"});
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
            std::string attempt = std::to_string(recovery.attempts()) + " of " + std::to_string(recovery.maxAttempts());
            if (rs == stability::Status::Opening) {
                centreTitle->setText(isEpisode() ? it.seriesName : it.title);
                centreText->setText(lastKnownPosition > 1 ? "Resuming at " + fmt::clock(lastKnownPosition) + "\xE2\x80\xA6"
                                                          : std::string("Opening" "\xE2\x80\xA6"));
            } else if (rs == stability::Status::Reconnecting) {
                centreTitle->setText("Reconnecting" "\xE2\x80\xA6");
                centreText->setText(secondsLeft > 0 ? "Connection lost. Next attempt in " + std::to_string(secondsLeft)
                                                      + " s  (attempt " + attempt + ")" : "Attempt " + attempt);
            } else if (rs == stability::Status::Refused) {
                centreTitle->setText("Provider temporarily refused the stream (HTTP 403)");
                centreText->setText("Retrying in " + std::to_string(secondsLeft) + " s" "\xE2\x80\xA6"
                                    "  (attempt " + attempt + ")");
                centreHint->setText("X  Retry now          Circle  Cancel");
            } else if (rs == stability::Status::Failed) {
                bool stopped = pb.state() == PlaybackState::Error;
                centreTitle->setText(stopped && !pb.errorMessage().empty() ? pb.errorMessage() : "This title is unavailable");
                centreText->setText(recovery.attempts() > 0 ? "Automatic reconnect gave up after "
                                                              + std::to_string(recovery.attempts()) + " attempts." : "");
                centreHint->setText("X  Retry          Circle  Back");
            }
            bool pillShown = !finished && rs == stability::Status::Buffering;
            pill->setVisibility(pillShown ? Visibility::Visible : Visibility::Hidden);
            if (pillShown) {
                pillSpinner->tick(now);
            }

            if (info->isVisible()) {
                char buf[1400];
                snprintf(buf, sizeof(buf),
                         "Container     %s\nResolution    %s\nFrame rate    %s\nVideo codec   %s\nPixel format  %s\n"
                         "Audio codec   %s\nAudio         %s\nBuffer        %.1f s\nNetwork       %s\nDropped       %lld\n"
                         "Tracks        %d audio, %d subtitle\nStability     %s\nState         %s\nRecovery      %s",
                         si.format.c_str(),
                         si.width > 0 ? (std::to_string(si.width) + " x " + std::to_string(si.height)).c_str() : "-",
                         si.fps > 0 ? diag::format("%.2f fps", si.fps).c_str() : "-",
                         si.videoCodec.empty() ? "-" : si.videoCodec.c_str(),
                         si.pixelFormat.empty() ? "-" : si.pixelFormat.c_str(),
                         si.audioCodec.empty() ? "-" : si.audioCodec.c_str(),
                         si.sampleRate > 0 ? diag::format("%d Hz, %s", si.sampleRate,
                                                          tracks::channelsName(si.channels).c_str()).c_str() : "-",
                         si.cacheSeconds,
                         si.cacheSpeed > 0 ? diag::format("%lld KB/s", si.cacheSpeed / 1000).c_str() : "-",
                         si.droppedFrames, countTracks(tracks::Kind::Audio), countTracks(tracks::Kind::Subtitle),
                         stabilityName(preset), statusName(rs),
                         (std::to_string(recovery.attempts()) + " / " + std::to_string(recovery.maxAttempts())
                          + (recovery.reason().empty() ? "" : ", last: " + recovery.reason())).c_str());
                infoText->setText(buf);
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
        enum class Row {
            AudioMenu,
            SubtitleMenu,
            SubtitleSize,
            SubtitlePosition,
            SubtitleShadow,
            TechInfo,
            AudioTrack,     // level 1
            SubtitleTrack,  // level 1 (id 0 = off)
            BackRow
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
            ph->setHints({{ui::Glyph::Cross, "Select"}, {ui::Glyph::Circle, "Back"}});
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
                    name = "Audio";
                    detail = a ? tracks::label(*a, a->id) + "  \xC2\xB7  " + tracks::details(*a)
                               : list.empty() ? "Available once playback starts" : "-";
                    break;
                }
                case Row::SubtitleMenu: {
                    const tracks::Track *sub = tracks::selected(list, tracks::Kind::Subtitle);
                    name = "Subtitles";
                    detail = sub ? tracks::label(*sub, sub->id) : countTracks(tracks::Kind::Subtitle) > 0 ? "Off"
                                                                                                         : "None in this file";
                    break;
                }
                case Row::SubtitleSize:
                    name = "Subtitle size";
                    detail = SIZE_NAMES[std::min(std::max(s.subtitleSize, 0), 2)];
                    break;
                case Row::SubtitlePosition:
                    name = "Subtitle position";
                    detail = POSITION_NAMES[s.subtitlePosition == 1 ? 1 : 0];
                    break;
                case Row::SubtitleShadow:
                    name = "Subtitle shadow";
                    detail = s.subtitleShadow ? "On" : "Off";
                    break;
                case Row::TechInfo:
                    name = "Technical info";
                    detail = info->isVisible() ? "Shown" : "Hidden";
                    break;
                case Row::AudioTrack:
                case Row::SubtitleTrack: {
                    if (pi.trackId == 0) {
                        name = "Off";
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
                    name = "Back";
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
            if (level == 0) {
                panelTitle->setText("Playback options");
                for (Row r: {Row::AudioMenu, Row::SubtitleMenu, Row::SubtitleSize, Row::SubtitlePosition,
                             Row::SubtitleShadow, Row::TechInfo}) {
                    panelItems.push_back({r, 0});
                }
            } else {
                tracks::Kind kind = level == 1 ? tracks::Kind::Audio : tracks::Kind::Subtitle;
                panelTitle->setText(level == 1 ? "Audio" : "Subtitles");
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
                    if (panelLevel > 0) {
                        showMenu(0, panelLevel == 1 ? 0 : 1);
                    } else {
                        closePanel();
                    }
                    return;
                case PadButton::Left:
                case PadButton::Right:
                case PadButton::Cross:
                    if (!e.repeat || e.button != PadButton::Cross) {
                        activate(panelItems[(size_t) panelList->selected()], e.button == PadButton::Left ? -1 : 1);
                    }
                    return;
                default:
                    return;
            }
        }

        void activate(const PanelItem &pi, int delta) {
            Settings &s = app.settings().get();
            Playback &pb = app.playback();
            bool appearance = false;
            switch (pi.row) {
                case Row::AudioMenu:
                    if (delta > 0) {
                        showMenu(1);
                    }
                    return;
                case Row::SubtitleMenu:
                    if (delta > 0) {
                        showMenu(2);
                    }
                    return;
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
                    showMenu(0);
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
        StabilityPreset preset = StabilityPreset::Balanced;
        stability::Recovery recovery;
        double knownDuration = 0;
        double lastKnownPosition = 0;
        double lastProgressMemory = 0;
        double lastProgressDisk = 0;
        bool pendingSeek = false;
        double seekTarget = 0;
        double seekCommitAt = 0;
        bool finished = false;
        double autoplayAt = 0;
        bool overlayVisible = true;
        double lastInput = 0;
        double lastRefresh = 0;
        bool subtitleWarningShown = false;
        bool languagesRecorded = false;
        // track choices made in the Options panel (this playback); languages carry over to the next episode
        int manualAudio = -1;
        int manualSub = -2;
        std::string manualAudioLang;
        std::string manualSubLang;

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
            return makeSection(app, "Playback", "Nothing to play.");
        }
        int i = std::min(std::max(index, 0), (int) queue.size() - 1);
        return new VodPlayerScreen(app, queue, i, resume, std::move(onExit));
    }
}
