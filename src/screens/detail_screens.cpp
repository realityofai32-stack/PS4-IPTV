// Movie and Series detail screens: the clean dark page, the framed poster on the left with only a small halo of its
// colour around it (screens::bindDetailArt, v7-ux-13 - no page-wide backdrop layer), and the text column next to it.
// Geometry: ui/layout.h (layout::detail), docs/v7-ui-v2-layouts.md.
//
// Both open at once with what the catalog already has (title, year, rating, poster; for series also plot,
// cast, genre) and request their detail response lazily (get_vod_info / get_series_info). The large poster
// is loaded as its own image kind; the grid poster (usually cached already) is shown until it arrives.
//
// Downloads: Movie details offer Download (then "Download 42%" with its actions, then Delete download once it
// is complete); Play / Resume use the downloaded file when there is one. Episode rows show their download
// state; Square downloads the focused episode, OPTIONS opens its actions (incl. downloading the whole season
// after a confirmation).

#include <cctype>

#include "common.h"
#include "../images/hero_art.h"
#include "../app/offline.h"
#include "../app/series_plan.h"
#include "../core/format.h"
#include "../platform/log.h"

using namespace c2d;
using namespace iptv;
using screens::VodItem;

namespace {

    std::string join(const std::vector<std::string> &parts) {
        std::string out;
        for (const auto &p: parts) {
            if (!p.empty()) {
                out += (out.empty() ? "" : "   \xC2\xB7   ") + p;
            }
        }
        return out;
    }

    std::string mediaLine(const MediaSummary &m, const std::string &extension) {
        std::string video = m.videoCodec == "h264" ? "H.264" : m.videoCodec == "hevc" ? "HEVC"
                                                                                       : m.videoCodec;
        std::string audio = m.audioCodec.empty() ? "" : tracks::codecName(m.audioCodec);
        if (!audio.empty() && m.audioChannels > 0) {
            audio += " " + tracks::channelsName(m.audioChannels);
        }
        std::string ext = extension;
        for (auto &c: ext) {
            c = (char) toupper((unsigned char) c);
        }
        return join({video, audio, ext});
    }

    // short state of a download for buttons and rows: "Download 42%", "Queued", "Paused" ...
    std::string downloadLabel(const dl::Item &d, const dl::LiveProgress &live) {
        switch (d.state) {
            case dl::State::Downloading: {
                int64_t bytes = d.key == live.key ? live.bytes : d.downloadedBytes;
                int64_t total = d.key == live.key ? live.total : d.expectedBytes;
                int pct = dl::percent(bytes, total);
                return pct >= 0 ? tr("download.button_pct", {std::to_string(pct)}) : tr("download.state.downloading");
            }
            case dl::State::Queued:
                return tr("download.state.queued");
            case dl::State::Paused:
                return tr("download.state.paused");
            case dl::State::WaitingForNetwork:
                return tr("download.state.waiting");
            case dl::State::Failed:
                return tr("download.state.failed");
            default:
                return tr("download.state.completed");
        }
    }

    // toast for an enqueue result
    void announceEnqueue(App &app, dl::DownloadManager::Enqueue r) {
        switch (r) {
            case dl::DownloadManager::Enqueue::Added:
                app.toast(tr(app.downloads().playbackActive() ? "download.queued_playback" : "download.queued"),
                          ToastKind::Success);
                break;
            case dl::DownloadManager::Enqueue::Resumed:
                app.toast(tr("download.resumed"), ToastKind::Success);
                break;
            case dl::DownloadManager::Enqueue::Exists:
                app.toast(tr("download.exists"));
                break;
            case dl::DownloadManager::Enqueue::Completed:
                app.toast(tr("download.already"));
                break;
            case dl::DownloadManager::Enqueue::NoProfile:
                app.toast(tr("download.problem.profile_missing"), ToastKind::Error);
                break;
            case dl::DownloadManager::Enqueue::NoSpace:
                app.toast(tr("download.problem.no_space"), ToastKind::Error);
                break;
        }
    }

    // poster: the version closest to the tile's size first (the large one for the 320 x 480 movie poster, the grid
    // one for the 180 x 270 series cover: ~1:1 texels, no blur and no shimmer), else the other, else the title
    void bindPoster(App &app, ui::PosterView *view, const std::string &title, const std::string &url,
                    bool preferLarge = true) {
        ImageKind first = preferLarge ? ImageKind::PosterLarge : ImageKind::Poster;
        ImageKind second = preferLarge ? ImageKind::Poster : ImageKind::PosterLarge;
        std::shared_ptr<ImageSet> img = app.images().get(first, url);
        if (!img) {
            img = app.images().get(second, url);
        }
        view->set(title, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
    }

    // ------------------------------------------------------------------ movie

    namespace L = layout::detail;

    const float BACKDROP_ANCHOR = 0.24f;

    class MovieDetailScreen : public Screen {
    public:
        MovieDetailScreen(App &a, Movie m) : Screen(a), movie(std::move(m)) {
            ui::background(this);
            // artwork (screens::bindDetailArt): the clean dark page, the framed poster and a small halo of its colour
            art.tint = new ui::Backdrop(FloatRect(0, 0, theme::SCREEN_W, theme::SCREEN_H), 0, 0, 0);
            add(art.tint);
            backdrop = new ui::Backdrop(FloatRect(L::BACKDROP_X, 0, theme::SCREEN_W - L::BACKDROP_X, L::M_BACKDROP_H),
                                        L::BACKDROP_FADE_LEFT, L::M_BACKDROP_FADE_BOTTOM, L::BACKDROP_FADE_TOP);
            backdrop->setAnchor(BACKDROP_ANCHOR);
            add(backdrop);
            art.backdrop = backdrop;
            art.posterRect = FloatRect(L::M_POSTER_X, L::M_POSTER_Y, L::M_POSTER_W, L::M_POSTER_H);
            art.textX = L::M_TEXT_X;
            screens::breadcrumb(this, tr("home.movies"));
            // the poster as a framed object (Home's showcase language): soft shadow, rounded, a thin frame
            screens::posterShadow(this, art.posterRect);
            poster = new ui::PosterView(art.posterRect, theme::HEADING);
            poster->setCornersRadius(theme::RADIUS);
            add(poster);
            screens::posterFrame(this, art.posterRect);
            const float x = L::M_TEXT_X;
            const float w = L::M_TEXT_W;
            // the title is the largest text on the page; long titles step down to the title size
            title = ui::label(this, movie.title, theme::DISPLAY, x, L::M_POSTER_Y - 10, ui::Weight::SemiBold,
                              theme::textStrong());
            if (title->width() > w * 1.6f) {
                title->setCharSize(theme::TITLE);
            }
            title->setMaxWidth(w);
            title->setMaxLines(2);
            meta = ui::label(this, "", theme::BODY, x, 0, ui::Weight::Regular, theme::textSecondary());
            meta->setMaxWidth(w);
            genre = ui::label(this, "", theme::LABEL, x, 0, ui::Weight::Regular, theme::textMuted());
            genre->setMaxWidth(w);
            plot = ui::label(this, "", theme::BODY, x, 0, ui::Weight::Regular, theme::textPrimary());
            plot->setMaxWidth(L::M_PLOT_W);
            plot->setMaxLines(4);
            progressText = ui::label(this, "", theme::LABEL, x, 0, ui::Weight::SemiBold, theme::textSecondary());
            bar = new ui::ProgressBar(FloatRect(x, 0, 480, theme::PROGRESS_H));
            add(bar);
            for (int i = 0; i < BUTTONS; i++) {
                buttons[i] = new ui::Button("", FloatRect(x, 0, i == 0 ? 360.0f : 270.0f, theme::BUTTON_H), i == 0);
                add(buttons[i]);
            }
            // supporting detail under the actions: people, then the technical line (never competing with the title)
            credits = ui::label(this, "", theme::LABEL, x, 0, ui::Weight::Regular, theme::textMuted());
            credits->setMaxWidth(L::M_PLOT_W);
            credits->setMaxLines(2);
            tech = ui::label(this, "", theme::CAPTION, x, 0, ui::Weight::Regular, theme::textMuted());
            tech->setMaxWidth(w);
            screens::hintBar(this, {{A::Confirm, tr("common.select")}, {A::Favorite, tr("common.favorite")},
                                    {A::Back, tr("common.back")}});
            app.vod().requestMovieInfo(app.session().profile, movie.streamId);
            refresh();
        }

        const char *name() const override { return "movie-detail"; }

        void onEnter() override {
            requestImages();
        }

        void onResume() override {
            requestImages();
            if (backFromPlayback) {
                backFromPlayback = false;
                focus = 0;   // the primary action (Resume / Play again), where the user started
            }
            refresh();
        }

        void onPause() override {
            app.images().want(std::vector<ImageRequest>());
        }

        void tick(double now) override {
            bool downloads = app.downloads().generation() != downloadsGen && now - lastDownloadRefresh >= 0.25;
            if (downloads || app.vod().generation() != vodGen || app.images().generation() != imageGen
                || app.library().generation() != libraryGen) {
                lastDownloadRefresh = now;
                refresh();
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            if (e.repeat && e.button != PadButton::Left && e.button != PadButton::Right) {
                return;
            }
            switch (e.button) {
                case PadButton::Left:
                    if (focus > 0) {
                        focus--;
                    }
                    break;
                case PadButton::Right:
                    if (focus + 1 < buttonCount) {
                        focus++;
                    }
                    break;
                case PadButton::Cross:
                    activate(actions[focus]);
                    return;
                case PadButton::Options:
                case PadButton::Triangle:
                    if (hasDownload) {
                        activate(Action::DownloadActions);
                    }
                    return;
                case PadButton::Square:
                    activate(Action::Favorite);
                    return;
                case PadButton::Circle:
                    app.pop();
                    return;
                default:
                    return;
            }
            refreshButtons();
        }

    private:
        enum class Action {
            Resume,
            Play,
            StartOver,
            Favorite,
            ResetProgress,
            Download,
            DownloadActions,
            DeleteDownload
        };

        static const int BUTTONS = 4;

        void requestImages() {
            if (app.settings().get().loadImages) {
                // the poster only: detail pages draw no backdrop (screens::bindDetailArt)
                std::vector<ImageRequest> req = {{ImageKind::PosterLarge, movie.icon}};
                req.push_back({ImageKind::Poster, movie.icon});
                app.images().want(req);
            }
        }

        VodItem item() const {
            VodItem it;
            it.type = ContentType::Movie;
            it.id = movie.streamId;
            it.extension = movie.extension.empty() ? "mkv" : movie.extension;
            it.title = movie.title;
            it.image = movie.icon;
            it.year = movie.year;
            std::shared_ptr<const MovieInfo> info = app.vod().movieInfo(movie.streamId);
            it.durationHint = info ? info->durationSeconds : 0;
            offline::preferLocal(app.downloads(), it, app.session().profile.id);   // the downloaded copy, if any
            return it;
        }

        dl::Item downloadItem() const {
            dl::Item d;
            d.kind = dl::Kind::Movie;
            d.profileId = app.session().profile.id;
            d.contentId = movie.streamId;
            d.title = movie.title;
            d.year = movie.year;
            d.extension = movie.extension.empty() ? "mkv" : movie.extension;
            d.poster = movie.icon;
            std::shared_ptr<const MovieInfo> info = app.vod().movieInfo(movie.streamId);
            d.durationHint = info ? info->durationSeconds : 0;
            return d;
        }

        void downloadActions() {
            dl::Item d;
            if (!app.downloads().find(app.session().profile.id, dl::Kind::Movie, movie.streamId, d)) {
                return;
            }
            std::vector<std::string> names;
            std::vector<std::function<void()>> acts;
            std::string key = d.key;
            if (d.state == dl::State::Completed) {
                names.push_back(tr("download.delete"));
                acts.push_back([this] { activate(Action::DeleteDownload); });
            } else {
                bool running = d.state == dl::State::Downloading || d.state == dl::State::Queued
                               || d.state == dl::State::WaitingForNetwork;
                if (d.problem == dl::Problem::RestartNeeded) {
                    names.push_back(tr("download.restart"));
                    acts.push_back([this, key] { app.downloads().confirmRestart(key); });
                } else {
                    names.push_back(tr(running ? "download.pause" : d.state == dl::State::Failed ? "download.retry"
                                                                                                  : "download.resume"));
                    acts.push_back([this, key, running] {
                        if (running) {
                            app.downloads().pause(key);
                        } else {
                            app.downloads().resume(key);
                        }
                    });
                }
                names.push_back(tr("download.cancel"));
                acts.push_back([this, key] {
                    app.push(screens::makeDialog(app, tr("download.cancel_title"), tr("download.cancel_text", {movie.title}),
                                                 {tr("common.cancel"), tr("download.cancel")}, guarded2([this, key](int c) {
                                if (c == 1) {
                                    app.downloads().cancel(key);
                                    app.toast(tr("download.cancelled"));
                                }
                            }), true));
                });
            }
            names.push_back(tr("downloads.open"));
            acts.push_back([this] { app.push(screens::makeDownloads(app)); });
            std::weak_ptr<bool> w = token;
            app.push(screens::makeMenu(app, tr("downloads.title"), names, -1, [w, acts](int i) {
                if (w.lock() && i >= 0 && i < (int) acts.size()) {
                    acts[(size_t) i]();
                }
            }));
        }

        std::function<void(int)> guarded2(std::function<void(int)> f) {
            std::weak_ptr<bool> w = token;
            return [w, f](int c) {
                if (w.lock()) {
                    f(c);
                }
            };
        }

        void activate(Action a) {
            switch (a) {
                case Action::Resume:
                case Action::Play:
                case Action::StartOver:
                    if (a == Action::StartOver) {
                        app.library().resetProgress(ContentType::Movie, movie.streamId);
                    }
                    backFromPlayback = true;
                    app.push(screens::makeVodPlayer(app, {item()}, 0, a == Action::Resume));
                    return;
                case Action::Favorite: {
                    bool on = app.library().toggleFavorite(ContentType::Movie, movie.streamId);
                    app.saveLibrary();
                    app.toast(tr(on ? "favorites.added" : "favorites.removed"), on ? ToastKind::Success
                                                                                  : ToastKind::Info);
                    break;
                }
                case Action::ResetProgress:
                    app.library().resetProgress(ContentType::Movie, movie.streamId);
                    app.saveLibrary();
                    app.toast(tr("progress.marked_unwatched"));
                    break;
                case Action::Download:
                    announceEnqueue(app, app.downloads().enqueue(downloadItem()));
                    break;
                case Action::DownloadActions:
                    downloadActions();
                    return;
                case Action::DeleteDownload: {
                    dl::Item d;
                    if (!app.downloads().find(app.session().profile.id, dl::Kind::Movie, movie.streamId, d)) {
                        return;
                    }
                    std::string key = d.key;
                    app.push(screens::makeDialog(app, tr("download.delete_title"), tr("download.delete_text", {movie.title}),
                                                 {tr("common.cancel"), tr("download.delete")}, guarded2([this, key](int c) {
                                if (c != 1) {
                                    return;
                                }
                                if (app.downloads().remove(key)) {
                                    app.toast(tr("download.deleted"), ToastKind::Success);   // progress is kept
                                } else {
                                    app.toast(tr("download.delete_failed"), ToastKind::Error);
                                }
                                refresh();
                            }), true));
                    return;
                }
            }
            refresh();
        }

        void refresh() {
            vodGen = app.vod().generation();
            imageGen = app.images().generation();
            libraryGen = app.library().generation();
            bindPoster(app, poster, movie.title, movie.icon);
            std::shared_ptr<const MovieInfo> info = app.vod().movieInfo(movie.streamId);
            if (info && info != backdropInfo) {
                // get_vod_info arrived: its backdrop_path (if any) replaces the poster-derived art
                backdropInfo = info;
                backdropUrl = info->backdrop;
                LOG_I("detail", "movie info: provider backdrop %s", backdropUrl.empty() ? "none" : "present");
                requestImages();
            }
            screens::bindDetailArt(app, art, movie.icon, backdropUrl);
            std::string error = app.vod().detailError("m" + movie.streamId);
            float rating = info && info->rating > 0 ? info->rating : movie.rating;
            int duration = info ? info->durationSeconds : 0;
            std::string res = info ? fmt::resolution(info->media.width, info->media.height) : "";
            // only what the provider sent: no empty labels, nothing invented
            meta->setText(join({movie.year > 0 ? std::to_string(movie.year) : "", fmt::rating(rating),
                                fmt::duration(duration), res}));
            genre->setText(info ? info->genre : "");
            tech->setText(info ? mediaLine(info->media, movie.extension) : "");
            credits->setText(info ? join({info->director.empty() ? "" : tr("detail.director", {info->director}),
                                          info->cast.empty() ? "" : tr("detail.cast", {info->cast})}) : "");
            if (info) {
                plot->setText(info->plot.empty() ? "" : info->plot);
                plot->setColor(theme::textPrimary());
            } else {
                plot->setText(error.empty() ? tr("detail.loading") : tr("detail.movie_unavailable", {error}));
                plot->setColor(theme::textMuted());
            }

            const HistoryEntry *p = app.library().progressOf(ContentType::Movie, movie.streamId);
            bool resumable = p && progress::inProgress(*p);
            bool watched = p && p->watched;
            if (resumable) {
                progressText->setText(tr("detail.stopped_at", {fmt::remaining(p->position, p->duration),
                                                               fmt::clock(p->position)}));
            } else {
                progressText->setText(watched ? "\xE2\x9C\x93  " + tr("progress.watched") : "");
            }
            progressText->setColor(watched ? theme::success() : theme::textSecondary());
            bar->setVisibility(resumable ? Visibility::Visible : Visibility::Hidden);
            if (resumable) {
                bar->setFraction((float) progress::fraction(p->position, p->duration));
            }

            // the text column, top to bottom: title, facts, genre, plot, progress, actions, people, technical line
            const float x = L::M_TEXT_X;
            float y = L::M_POSTER_Y - 10 + title->height() + 12;
            meta->setPosition(x, y);
            y += meta->getText().empty() ? 0 : meta->height() + 6;
            genre->setPosition(x, y);
            y += genre->getText().empty() ? 8 : genre->height() + 26;
            plot->setPosition(x, y);
            y += plot->getText().empty() ? 0 : plot->height() + 30;
            if (!progressText->getText().empty()) {
                progressText->setPosition(x, y);
                y += progressText->height() + 12;
                bar->setPosition(x, y);
                y += resumable ? theme::PROGRESS_H + 30 : 18;
            }
            buttonsY = std::max(y, L::M_BUTTONS_MIN_Y);
            y = buttonsY + theme::BUTTON_H + 40;
            credits->setPosition(x, y);
            y += credits->getText().empty() ? 0 : credits->height() + 10;
            tech->setPosition(x, y);

            bool fav = app.library().isFavorite(ContentType::Movie, movie.streamId);
            downloadsGen = app.downloads().generation();
            dl::Item d;
            hasDownload = app.downloads().find(app.session().profile.id, dl::Kind::Movie, movie.streamId, d);
            bool local = hasDownload && d.state == dl::State::Completed
                         && !offline::localFile(app.downloads(), app.session().profile.id, ContentType::Movie,
                                                movie.streamId).empty();
            buttonCount = 0;
            auto addButton = [this](Action a, const std::string &text) {
                actions[buttonCount] = a;
                buttons[buttonCount]->setText(text);
                buttonCount++;
            };
            if (resumable) {
                // exactly there; from the downloaded file when there is one
                addButton(Action::Resume, "\xE2\x96\xB6  " + tr(local ? "detail.resume_offline" : "detail.resume",
                                                                {fmt::clock(p->position)}));
                addButton(Action::StartOver, tr("common.start_over"));
            } else {
                addButton(Action::Play, "\xE2\x96\xB6  " + tr(local ? (watched ? "detail.play_again_offline" : "download.play_offline")
                                                                     : (watched ? "detail.play_again" : "common.play")));
                if (watched) {
                    addButton(Action::ResetProgress, tr("progress.mark_unwatched"));
                }
            }
            if (local) {
                addButton(Action::DeleteDownload, tr("download.delete"));
            } else if (hasDownload) {
                addButton(Action::DownloadActions, "\xE2\x86\x93  " + downloadLabel(d, app.downloads().live()));
            } else if (app.session().profile.supportsDownloads()) {
                // (Stalker portals: single-use play links, no downloads)
                addButton(Action::Download, "\xE2\x86\x93  " + tr("download.download"));
            }
            addButton(Action::Favorite, fav ? "\xE2\x98\x85  " + tr("favorites.in_favorites")
                                            : "\xE2\x98\x86  " + tr("favorites.add"));
            focus = std::min(focus, buttonCount - 1);
            refreshButtons();
        }

        void refreshButtons() {
            // the primary action (Play / Resume) first and widest; the others follow on the same line
            float x = L::M_TEXT_X;
            for (int i = 0; i < BUTTONS; i++) {
                buttons[i]->setVisibility(i < buttonCount ? Visibility::Visible : Visibility::Hidden);
                buttons[i]->setPosition(x, buttonsY);
                buttons[i]->setFocused(i == focus);
                x += buttons[i]->getSize().x + theme::SPACE_M;
            }
        }

        Movie movie;
        screens::DetailArt art;
        ui::Backdrop *backdrop;
        std::string backdropUrl;                          // the provider's landscape backdrop ("" = none)
        std::shared_ptr<const MovieInfo> backdropInfo;    // the info backdropUrl was read from
        ui::PosterView *poster;
        ui::Label *title;
        ui::Label *meta;
        ui::Label *genre;
        ui::Label *tech;
        ui::Label *credits;
        ui::Label *plot;
        ui::Label *progressText;
        ui::ProgressBar *bar;
        float buttonsY = 600;
        ui::Button *buttons[BUTTONS];
        Action actions[BUTTONS] = {Action::Play, Action::Favorite, Action::Favorite, Action::Favorite};
        int buttonCount = 0;
        int focus = 0;
        bool backFromPlayback = false;
        bool hasDownload = false;
        unsigned vodGen = 0;
        unsigned imageGen = 0;
        unsigned libraryGen = 0;
        unsigned downloadsGen = 0;
        double lastDownloadRefresh = 0;
        std::shared_ptr<bool> token = std::make_shared<bool>(true);
    };

    // ------------------------------------------------------------------ series

    const float CHIP_GAP = 12;

    class SeriesDetailScreen : public Screen, public ui::ListView::Adapter {
    public:
        SeriesDetailScreen(App &a, Series s) : Screen(a), series(std::move(s)) {
            ui::background(this);
            // artwork (screens::bindDetailArt): the clean dark page, the framed cover and a small halo of its colour
            art.tint = new ui::Backdrop(FloatRect(0, 0, theme::SCREEN_W, theme::SCREEN_H), 0, 0, 0);
            add(art.tint);
            backdrop = new ui::Backdrop(FloatRect(L::BACKDROP_X, 0, theme::SCREEN_W - L::BACKDROP_X, L::S_BACKDROP_H),
                                        L::BACKDROP_FADE_LEFT, L::S_BACKDROP_FADE_BOTTOM, L::BACKDROP_FADE_TOP);
            backdrop->setAnchor(BACKDROP_ANCHOR);
            add(backdrop);
            art.backdrop = backdrop;
            art.posterRect = FloatRect(L::S_COVER_X, L::S_COVER_Y, L::S_COVER_W, L::S_COVER_H);
            art.textX = L::S_TEXT_X;
            screens::breadcrumb(this, tr("home.series"));
            screens::posterShadow(this, art.posterRect);
            cover = new ui::PosterView(art.posterRect, theme::BODY);
            cover->setCornersRadius(theme::RADIUS);
            add(cover);
            screens::posterFrame(this, art.posterRect);
            const float x = L::S_TEXT_X;
            title = ui::label(this, series.title, theme::TITLE, x, L::S_COVER_Y - 6, ui::Weight::SemiBold,
                              theme::textStrong());
            if (title->width() > L::S_TEXT_W) {
                title->setCharSize(theme::HEADING);   // one line: the actions keep their place
            }
            title->setMaxWidth(L::S_TEXT_W);
            meta = ui::label(this, "", theme::BODY, x, 0, ui::Weight::Regular, theme::textSecondary());
            meta->setMaxWidth(L::S_TEXT_W);
            plot = ui::label(this, "", theme::LABEL, x, 0, ui::Weight::Regular, theme::textPrimary());
            plot->setMaxWidth(L::S_PLOT_W);
            plot->setMaxLines(2);   // a short plot: the actions keep their line, bottom-aligned with the cover
            for (int i = 0; i < 2; i++) {
                // the main button holds "Resume S01E03 · 1:02:15"
                float bw = i == 0 ? L::S_RESUME_W : L::S_FAVORITE_W;
                buttons[i] = new ui::Button("", FloatRect(x + (float) i * (L::S_RESUME_W + theme::SPACE_M), L::S_BUTTONS_Y,
                                                          bw, L::S_BUTTON_H), i == 0);
                add(buttons[i]);
            }
            seasonLayer = new RectangleShape(FloatRect(theme::SAFE_X, L::S_SEASONS_Y, theme::CONTENT_W, L::S_CHIP_H));
            seasonLayer->setFillColor(theme::none());
            add(seasonLayer);
            episodes = new ui::ListView(FloatRect(theme::SAFE_X, L::S_EPISODES_Y, theme::CONTENT_W, L::S_EPISODES_H),
                                        L::S_ROW_H, L::S_ROW_GAP, this);
            add(episodes);
            stateText = ui::label(this, "", theme::BODY, theme::SAFE_X, L::S_EPISODES_Y + 110, ui::Weight::Regular,
                                  theme::textMuted());
            stateText->setAlign(ui::Align::Center, theme::CONTENT_W);
            stateText->setMaxWidth(1400);
            stateText->setMaxLines(2);
            spinner = new ui::Spinner(18);
            spinner->setPosition((theme::SCREEN_W - 18 * 3.6f) / 2, L::S_EPISODES_Y + 190);
            add(spinner);
            hints = screens::hintBar(this, {});
            app.vod().requestSeriesInfo(app.session().profile, series.seriesId);
            refreshAll();
        }

        const char *name() const override { return "series-detail"; }

        void onEnter() override {
            requestImages();
        }

        void onPause() override {
            app.images().want(std::vector<ImageRequest>());
        }

        void onInputDeviceChanged() override {
            refreshAll();   // "Press {key} to try again"
        }

        void onResume() override {
            requestImages();
            // back from an episode: select the episode that was playing last
            if (!returnEpisode.empty() && info) {
                for (size_t s = 0; s < info->seasons.size(); s++) {
                    const auto &eps = info->seasons[s].episodes;
                    for (size_t e = 0; e < eps.size(); e++) {
                        if (eps[e].id == returnEpisode) {
                            seasonIndex = (int) s;
                            episodes->setSelected((int) e);
                            setFocus(2);
                        }
                    }
                }
                returnEpisode.clear();
            }
            refreshAll();
        }

        void tick(double now) override {
            if (app.vod().generation() != vodGen || app.library().generation() != libraryGen) {
                refreshAll();
                redraw();
            }
            if (app.downloads().generation() != downloadsGen && now - lastDownloadRefresh >= 0.25) {
                lastDownloadRefresh = now;   // episode download states / percentages
                downloadsGen = app.downloads().generation();
                liveDownload = app.downloads().live();
                episodes->reload();
                redraw();
            }
            if (app.images().generation() != imageGen) {
                imageGen = app.images().generation();
                bindPoster(app, cover, series.title, series.cover, false);
                screens::bindDetailArt(app, art, series.cover, backdropUrl());
                episodes->reload();   // episode stills
                redraw();
            }
            if (spinner->isVisible() && spinner->tick(now)) {
                redraw();
            }
        }

        // ------------------------------------------------------------------ episode rows
        int count() override {
            return info && seasonIndex < (int) info->seasons.size()
                   ? (int) info->seasons[(size_t) seasonIndex].episodes.size() : 0;
        }

        C2DObject *createRow(float w, float h) override {
            EpisodeRow r;
            r.bg = new ui::RowFrame(w, h, 0);
            // the still when the provider has one; otherwise a text tile with the episode number (never a fake image)
            const float tx = 16;
            r.thumb = new ui::PosterView(FloatRect(tx, (h - L::S_THUMB_H) / 2, L::S_THUMB_W, L::S_THUMB_H), theme::LABEL);
            r.thumb->setCornersRadius(theme::RADIUS_SMALL);
            // source-aware (ui/image_fit.h Poster): a real 16:9 still fills the tile; a portrait image (providers often
            // repeat the series cover) is shown whole instead of being cropped to a 16:9 fragment
            r.bg->add(r.thumb);
            const float x = tx + L::S_THUMB_W + 28;
            r.name = ui::label(r.bg, "", theme::BODY, x, 20);
            r.name->setMaxWidth(w - x - 360);
            r.code = ui::label(r.bg, "", theme::LABEL, x, 60, ui::Weight::SemiBold, theme::textSecondary());
            r.detail = ui::label(r.bg, "", theme::LABEL, x, 60, ui::Weight::Regular, theme::textMuted());
            r.detail->setMaxWidth(w - x - 360 - 120);
            r.state = ui::label(r.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h), ui::Weight::SemiBold,
                                theme::success());
            r.state->setAlign(ui::Align::Right, w - 28);
            // in progress: "24:16 / 57:00" above a bar
            r.time = ui::label(r.bg, "", theme::CAPTION, 0, ui::Label::centerOffset(theme::CAPTION, h) - 14,
                               ui::Weight::SemiBold, theme::textSecondary());
            r.time->setAlign(ui::Align::Right, w - 28);
            r.bar = new ui::ProgressBar(FloatRect(w - 28 - 240, h / 2 + 14, 240, theme::PROGRESS_H));
            r.bg->add(r.bar);
            rows.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int index, bool selected, bool focused) override {
            const Season &season = info->seasons[(size_t) seasonIndex];
            const Episode &ep = season.episodes[(size_t) index];
            const HistoryEntry *p = app.library().progressOf(ContentType::Series, ep.id);
            seriesplan::EpisodeState state = seriesplan::episodeState(p);
            bool partial = state == seriesplan::EpisodeState::InProgress;
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                // the provider's episode image whenever one is loaded (the pre-redesign behaviour); the "E 3" tile only
                // when the episode has none (or it could not be loaded)
                std::shared_ptr<ImageSet> still = ep.image.empty() ? nullptr : app.images().get(ImageKind::Poster, ep.image);
                r.thumb->set(tr("episode.short", {std::to_string(ep.number)}), still ? still->at(0).texture : nullptr,
                             still ? still->at(0).size : Vector2i());
                r.code->setText(fmt::episodeCode(ep.season, ep.number));
                r.name->setText(ep.title.empty() ? tr("episode.number", {std::to_string(ep.number)}) : ep.title);
                r.name->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                r.name->setColor(focused ? theme::textStrong() : theme::textPrimary());
                std::string detail = join({fmt::duration(ep.durationSeconds), fmt::resolution(ep.media.width, ep.media.height)});
                // "✓ Watched", and the download: "↓ Downloaded" / "↓ 42%" / "↓ Queued"...
                std::string st = state == seriesplan::EpisodeState::Watched ? "\xE2\x9C\x93  " + tr("progress.watched") : "";
                dl::Item d;
                bool hasDl = app.downloads().find(app.session().profile.id, dl::Kind::Episode, ep.id, d);
                if (hasDl) {
                    std::string ds = "\xE2\x86\x93 " + (d.state == dl::State::Completed ? tr("download.badge")
                                                                                         : downloadLabel(d, liveDownload));
                    if (partial) {
                        // the right side shows the position and its bar: the download goes on the detail line
                        detail = join({detail, ds});
                    } else {
                        st = st.empty() ? ds : ds + "   " + st;
                    }
                }
                r.detail->setText(detail.empty() ? "" : "\xC2\xB7  " + detail);
                r.detail->setPosition(r.code->getPosition().x + r.code->width() + 10, r.code->getPosition().y);
                r.state->setText(st);
                r.state->setColor(hasDl && d.state == dl::State::Failed ? theme::danger() : theme::success());
                double total = p && p->duration > 0 ? p->duration : ep.durationSeconds;
                r.time->setText(partial ? (total > 0 ? fmt::clock(p->position) + " / " + fmt::clock(total)
                                                     : fmt::clock(p->position)) : "");
                r.bar->setVisibility(partial ? Visibility::Visible : Visibility::Hidden);
                if (partial) {
                    r.bar->setFraction((float) progress::fraction(p->position, total));
                }
                r.bg->setState(focused, selected && !focused && focusOnEpisodesRow());
                r.thumb->setDimmed(!focused);
            }
        }

        // the episode cursor is shown (accent edge) while the seasons have the focus: where Down leads
        bool focusOnEpisodesRow() const { return focus == 1; }

        // ------------------------------------------------------------------ input
        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    if (focus == 2 && episodes->selected() > 0) {
                        episodes->moveSelection(-1);
                    } else if (e.repeat) {
                        return;   // a held direction scrolls the episodes, it never leaves the list
                    } else if (focus == 2) {
                        setFocus(hasSeasons() ? 1 : 0);
                    } else if (focus == 1) {
                        setFocus(0);
                    }
                    break;
                case PadButton::Down:
                    if (focus == 2) {
                        episodes->moveSelection(1);
                    } else if (e.repeat) {
                        return;
                    } else if (focus == 0) {
                        setFocus(hasSeasons() ? 1 : focus);
                    } else if (focus == 1 && count() > 0) {
                        setFocus(2);
                    }
                    break;
                case PadButton::Left:
                    if (focus == 0 && buttonFocus > 0) {
                        buttonFocus--;
                    } else if (focus == 1) {
                        selectSeason(seasonIndex - 1);
                    }
                    break;
                case PadButton::Right:
                    if (focus == 0 && buttonFocus + 1 < buttonCount) {
                        buttonFocus++;
                    } else if (focus == 1) {
                        selectSeason(seasonIndex + 1);
                    }
                    break;
                case PadButton::L1:
                case PadButton::R1:
                    if (!e.repeat) {
                        selectSeason(seasonIndex + (e.button == PadButton::L1 ? -1 : 1));
                    }
                    break;
                case PadButton::L2:
                case PadButton::R2:
                    if (focus == 2) {
                        episodes->moveSelection((e.button == PadButton::L2 ? -1 : 1) * episodes->pageSize());
                    }
                    break;
                case PadButton::Cross:
                    if (e.repeat) {
                        return;
                    }
                    if (focus == 0) {
                        activate(actions[buttonFocus]);
                    } else if (focus == 1) {
                        if (count() > 0) {
                            setFocus(2);
                        } else if (!info) {
                            app.vod().requestSeriesInfo(app.session().profile, series.seriesId, true);
                        }
                    } else if (count() > 0) {
                        playEpisode(seasonIndex, episodes->selected(), true);
                    }
                    return;
                case PadButton::Square:
                    if (!e.repeat) {
                        if (focus == 2 && count() > 0 && app.session().profile.supportsDownloads()) {
                            episodeDownload(seasonIndex, episodes->selected());   // download / pause / resume
                        } else {
                            activate(Action::Favorite);
                        }
                    }
                    return;
                case PadButton::Options:
                case PadButton::Triangle:
                    if (!e.repeat && focus == 2 && count() > 0) {
                        episodeMenu(seasonIndex, episodes->selected());
                    }
                    return;
                case PadButton::Circle:
                    if (!e.repeat) {
                        app.pop();
                    }
                    return;
                default:
                    return;
            }
            refreshFocus();
        }

    private:
        enum class Action {
            Resume,
            Favorite,
            Retry
        };

        struct EpisodeRow {
            ui::RowFrame *bg;
            ui::PosterView *thumb;
            ui::Label *code;
            ui::Label *name;
            ui::Label *detail;
            ui::Label *state;
            ui::Label *time;
            ui::ProgressBar *bar;
        };

        bool hasSeasons() const { return info && !info->seasons.empty(); }

        // the provider's landscape backdrop from get_series_info ("" = none or not loaded yet)
        std::string backdropUrl() const { return info ? info->series.backdrop : std::string(); }

        // the cover (large, then grid size) and the stills of the episodes on screen and a few below
        void requestImages() {
            if (!app.settings().get().loadImages) {
                return;
            }
            // the cover and the episode stills: detail pages draw no backdrop (screens::bindDetailArt)
            std::vector<ImageRequest> req = {{ImageKind::Poster, series.cover}, {ImageKind::PosterLarge, series.cover}};
            if (info && seasonIndex < (int) info->seasons.size()) {
                const auto &eps = info->seasons[(size_t) seasonIndex].episodes;
                int first = std::max(0, episodes->firstVisible() - 1);
                int last = std::min((int) eps.size() - 1, episodes->firstVisible() + episodes->visibleCount() + 3);
                for (int i = first; i <= last; i++) {
                    if (!eps[(size_t) i].image.empty()) {
                        req.push_back({ImageKind::Poster, eps[(size_t) i].image});
                    }
                }
            }
            app.images().want(req);
        }

        // every episode of every season, in order: the player's "next episode" queue
        // the chosen episode; its neighbours come from screens::seriesEpisodeQueue (the shared episode setup)
        VodItem episodeVodItem(const Episode &ep) const {
            VodItem it;
            it.type = ContentType::Series;
            it.id = ep.id;
            it.extension = ep.extension.empty() ? "mkv" : ep.extension;
            it.title = ep.title;
            it.image = series.cover;
            it.year = series.year;
            it.seriesId = series.seriesId;
            it.seriesName = series.title;
            it.season = ep.season;
            it.episode = ep.number;
            it.durationHint = ep.durationSeconds;
            it.cmd = ep.cmd;
            it.linkSeries = ep.linkSeries;
            offline::preferLocal(app.downloads(), it, app.session().profile.id);   // a downloaded episode
            return it;
        }

        dl::Item episodeItem(const Episode &ep) const {
            dl::Item d;
            d.kind = dl::Kind::Episode;
            d.profileId = app.session().profile.id;
            d.contentId = ep.id;
            d.seriesId = series.seriesId;
            d.seriesName = series.title;
            d.season = ep.season;
            d.episode = ep.number;
            d.title = ep.title;
            d.year = series.year;
            d.extension = ep.extension.empty() ? "mkv" : ep.extension;
            d.poster = series.cover;
            d.durationHint = ep.durationSeconds;
            return d;
        }

        // Square on an episode: download it, or pause / resume its download
        void episodeDownload(int season, int index) {
            const Episode &ep = info->seasons[(size_t) season].episodes[(size_t) index];
            dl::Item d;
            if (app.downloads().find(app.session().profile.id, dl::Kind::Episode, ep.id, d)) {
                if (d.state == dl::State::Completed) {
                    episodeMenu(season, index);
                } else if (d.state == dl::State::Downloading || d.state == dl::State::Queued
                           || d.state == dl::State::WaitingForNetwork) {
                    app.downloads().pause(d.key);
                    app.toast(tr("download.paused_toast"));
                } else if (d.problem == dl::Problem::RestartNeeded) {
                    episodeMenu(season, index);
                } else {
                    app.downloads().resume(d.key);
                    app.toast(tr("download.resumed"));
                }
                return;
            }
            announceEnqueue(app, app.downloads().enqueue(episodeItem(ep)));
        }

        void episodeMenu(int season, int index) {
            const Episode &ep = info->seasons[(size_t) season].episodes[(size_t) index];
            std::vector<std::string> names;
            std::vector<std::function<void()>> acts;
            const HistoryEntry *p = app.library().progressOf(ContentType::Series, ep.id);
            bool partial = seriesplan::episodeState(p) == seriesplan::EpisodeState::InProgress;
            dl::Item d;
            bool hasDl = app.downloads().find(app.session().profile.id, dl::Kind::Episode, ep.id, d);
            bool local = hasDl && d.state == dl::State::Completed;
            names.push_back(partial ? tr(local ? "detail.resume_offline" : "detail.resume", {fmt::clock(p->position)})
                                    : tr(local ? "download.play_offline" : "common.play"));
            acts.push_back([this, season, index] { playEpisode(season, index, true); });
            std::string key = hasDl ? d.key : "";
            std::string name = fmt::episodeCode(ep.season, ep.number);
            bool downloadable = app.session().profile.supportsDownloads();
            if (!downloadable) {
                // a Stalker portal: play only
            } else if (!hasDl) {
                names.push_back(tr("download.download_episode"));
                acts.push_back([this, season, index] { episodeDownload(season, index); });
            } else if (local) {
                names.push_back(tr("download.delete"));
                acts.push_back([this, key, name] {
                    app.push(screens::makeDialog(app, tr("download.delete_title"), tr("download.delete_text", {name}),
                                                 {tr("common.cancel"), tr("download.delete")}, guardedChoice([this, key](int c) {
                                if (c == 1) {
                                    app.toast(tr(app.downloads().remove(key) ? "download.deleted" : "download.delete_failed"));
                                    episodes->reload();
                                }
                            }), true));
                });
            } else {
                bool running = d.state == dl::State::Downloading || d.state == dl::State::Queued
                               || d.state == dl::State::WaitingForNetwork;
                if (d.problem == dl::Problem::RestartNeeded) {
                    names.push_back(tr("download.restart"));
                    acts.push_back([this, key] { app.downloads().confirmRestart(key); });
                } else {
                    names.push_back(tr(running ? "download.pause" : d.state == dl::State::Failed ? "download.retry"
                                                                                                  : "download.resume"));
                    acts.push_back([this, key, running] {
                        if (running) {
                            app.downloads().pause(key);
                        } else {
                            app.downloads().resume(key);
                        }
                    });
                }
                names.push_back(tr("download.cancel"));
                acts.push_back([this, key] { app.downloads().cancel(key); });
            }
            const Season &s = info->seasons[(size_t) season];
            if (downloadable) {
                names.push_back(tr("download.download_season", {std::to_string(s.number)}));
                acts.push_back([this, season] { confirmSeasonDownload(season); });
                names.push_back(tr("downloads.open"));
                acts.push_back([this] { app.push(screens::makeDownloads(app)); });
            }
            std::weak_ptr<bool> w = aliveToken;
            app.push(screens::makeMenu(app, name + "  " + (ep.title.empty() ? tr("episode.number", {std::to_string(ep.number)})
                                                                            : ep.title),
                                       names, -1, [w, acts](int i) {
                        if (w.lock() && i >= 0 && i < (int) acts.size()) {
                            acts[(size_t) i]();
                        }
                    }));
        }

        // every episode of a season that is not downloaded yet, after the user agrees
        void confirmSeasonDownload(int season) {
            const Season &s = info->seasons[(size_t) season];
            int missing = 0;
            for (const Episode &ep: s.episodes) {
                dl::Item d;
                missing += app.downloads().find(app.session().profile.id, dl::Kind::Episode, ep.id, d) ? 0 : 1;
            }
            if (missing == 0) {
                app.toast(tr("download.season_done"));
                return;
            }
            app.push(screens::makeDialog(app, tr("download.season_title", {std::to_string(s.number)}),
                                         tr("download.season_text", {i18n::count("download.episodes", missing)}),
                                         {tr("common.cancel"), tr("download.download")}, guardedChoice([this, season](int c) {
                        if (c != 1 || !info || season >= (int) info->seasons.size()) {
                            return;
                        }
                        int added = 0;
                        for (const Episode &ep: info->seasons[(size_t) season].episodes) {
                            added += app.downloads().enqueue(episodeItem(ep)) == dl::DownloadManager::Enqueue::Added;
                        }
                        app.toast(i18n::count("download.season_queued", added), ToastKind::Success);
                        episodes->reload();
                    })));
        }

        std::function<void(int)> guardedChoice(std::function<void(int)> f) {
            std::weak_ptr<bool> w = aliveToken;
            return [w, f](int c) {
                if (w.lock()) {
                    f(c);
                }
            };
        }

        void playEpisode(int season, int episode, bool resume) {
            if (!info || season < 0 || season >= (int) info->seasons.size() || episode < 0
                || episode >= (int) info->seasons[(size_t) season].episodes.size()) {
                return;
            }
            VodItem it = episodeVodItem(info->seasons[(size_t) season].episodes[(size_t) episode]);
            std::weak_ptr<bool> alive = aliveToken;
            app.push(screens::playSeriesEpisode(app, it, resume, [this, alive](const std::string &id) {
                if (alive.lock()) {
                    returnEpisode = id;
                }
            }));
        }

        seriesplan::Action plan() const {
            return info ? seriesplan::defaultAction(*info, series.seriesId, app.library()) : seriesplan::Action();
        }

        void activate(Action a) {
            switch (a) {
                case Action::Resume: {
                    seriesplan::Action a = plan();
                    if (a.kind != seriesplan::Kind::None) {
                        playEpisode(a.season, a.episode, a.kind == seriesplan::Kind::Resume);
                    }
                    return;
                }
                case Action::Favorite: {
                    bool on = app.library().toggleFavorite(ContentType::Series, series.seriesId);
                    app.saveLibrary();
                    app.toast(tr(on ? "favorites.added" : "favorites.removed"), on ? ToastKind::Success
                                                                                  : ToastKind::Info);
                    refreshAll();
                    return;
                }
                case Action::Retry:
                    app.vod().requestSeriesInfo(app.session().profile, series.seriesId, true);
                    refreshAll();
                    return;
            }
        }

        void selectSeason(int s) {
            if (!hasSeasons() || s < 0 || s >= (int) info->seasons.size() || s == seasonIndex) {
                return;
            }
            seasonIndex = s;
            episodes->setSelected(0);
            episodes->reload();
            buildChips();
            if (focus == 2 && count() == 0) {
                setFocus(1);
            }
        }

        void setFocus(int f) {
            focus = f;
            refreshFocus();
        }

        // the header's short text: the series plot, or - while the episodes have the focus - the focused episode's
        // own synopsis (when the provider has one), so the top tells what the selected row is about
        void showPlot() {
            const Episode *ep = nullptr;
            if (focus == 2 && info && seasonIndex < (int) info->seasons.size()) {
                const auto &eps = info->seasons[(size_t) seasonIndex].episodes;
                int i = episodes->selected();
                if (i >= 0 && i < (int) eps.size() && !eps[(size_t) i].plot.empty()) {
                    ep = &eps[(size_t) i];
                }
            }
            std::string text = ep ? fmt::episodeCode(ep->season, ep->number) + "  \xC2\xB7  " + ep->plot : series.plot;
            if (text != plot->getText()) {
                plot->setText(text);
                plot->setColor(ep ? theme::textSecondary() : theme::textPrimary());
            }
        }

        void refreshFocus() {
            showPlot();
            for (int i = 0; i < 2; i++) {
                buttons[i]->setVisibility(i < buttonCount ? Visibility::Visible : Visibility::Hidden);
                buttons[i]->setFocused(focus == 0 && i == buttonFocus);
            }
            episodes->setFocused(focus == 2);
            buildChips();
            requestImages();   // stills of the episodes now on screen
            if (focus == 2) {
                const bool dl = app.session().profile.supportsDownloads();
                hints->setHints({{A::Confirm, tr("common.play")},
                                 {dl ? A::Download : A::Favorite, tr(dl ? "download.download" : "common.favorite")},
                                 {A::Options, tr("detail.episode_options")},
                                 {A::Tabs, tr("detail.season")}, {A::Back, tr("common.back")}});
            } else {
                hints->setHints({{A::Confirm, tr("common.select")}, {A::Tabs, tr("detail.season")},
                                 {A::Favorite, tr("common.favorite")}, {A::Back, tr("common.back")}});
            }
        }

        // season chips: a window of chips around the selected season
        void buildChips() {
            for (auto *c: seasonLayer->getChilds()) {
                seasonLayer->remove(c);
                delete c;
            }
            if (!hasSeasons()) {
                return;
            }
            int n = (int) info->seasons.size();
            // chips fitted to their text (at least S_CHIP_MIN_W); a window around the selected season when they
            // do not all fit on the line
            std::vector<ui::Chip *> chips;
            for (int i = 0; i < n; i++) {
                const Season &s = info->seasons[(size_t) i];
                std::string text = s.number > 0 ? tr("detail.season_number", {std::to_string(s.number)})
                                                : tr("detail.specials");
                auto *chip = new ui::Chip(text, 0, 0, 0, L::S_CHIP_H);
                if (chip->width() < L::S_CHIP_MIN_W) {
                    delete chip;
                    chip = new ui::Chip(text, 0, 0, L::S_CHIP_MIN_W, L::S_CHIP_H);
                }
                chip->setState(i == seasonIndex, i == seasonIndex && focus == 1);
                chips.push_back(chip);
            }
            const float room = seasonLayer->getSize().x;
            auto fits = [&](int from, int to) {   // chips from..to (inclusive) on one line
                float w = 0;
                for (int i = from; i <= to; i++) {
                    w += chips[(size_t) i]->width() + (i > from ? CHIP_GAP : 0);
                }
                return w <= room;
            };
            int first = 0;
            while (first < seasonIndex && !fits(first, seasonIndex)) {
                first++;
            }
            float x = 0;
            for (int i = 0; i < n; i++) {
                ui::Chip *chip = chips[(size_t) i];
                if (i < first || x + chip->width() > room) {
                    delete chip;
                    continue;
                }
                chip->setPosition(x, 0);
                seasonLayer->add(chip);
                x += chip->width() + CHIP_GAP;
            }
        }

        void refreshAll() {
            vodGen = app.vod().generation();
            libraryGen = app.library().generation();
            std::shared_ptr<const SeriesInfo> fresh = app.vod().seriesInfo(series.seriesId);
            if (fresh && fresh != info) {
                info = fresh;
                // prefer the detail response's text when the list entry had none
                const Series &d = info->series;
                if (series.plot.empty()) {
                    series.plot = d.plot;
                }
                if (series.cast.empty()) {
                    series.cast = d.cast;
                }
                if (series.director.empty()) {
                    series.director = d.director;
                }
                if (series.genre.empty()) {
                    series.genre = d.genre;
                }
                seriesplan::Action a = plan();
                if (a.kind != seriesplan::Kind::None && returnEpisode.empty()) {
                    seasonIndex = a.season;   // open on the episode the main button plays
                    episodes->setSelected(a.episode);
                }
                LOG_I("detail", "series info: provider backdrop %s", backdropUrl().empty() ? "none" : "present");
                requestImages();   // the backdrop (if any) and the stills of the season shown
            }
            imageGen = app.images().generation();
            bindPoster(app, cover, series.title, series.cover, false);
            screens::bindDetailArt(app, art, series.cover, backdropUrl());
            meta->setText(join({series.year > 0 ? std::to_string(series.year) : "", fmt::rating(series.rating),
                                series.genre,
                                series.runtimeMinutes > 0 ? tr("detail.per_episode", {std::to_string(series.runtimeMinutes)}) : "",
                                hasSeasons() ? i18n::count("detail.seasons", (long long) info->seasons.size()) : ""}));
            showPlot();
            // title, facts, short plot; the actions are bottom-aligned with the cover (people are left out:
            // the top stays compact so four episodes are on screen)
            float y = L::S_COVER_Y - 6 + title->height() + 10;
            meta->setPosition(L::S_TEXT_X, y);
            y += meta->height() + 10;
            plot->setPosition(L::S_TEXT_X, y);

            std::string error = app.vod().detailError("s" + series.seriesId);
            bool loading = !info && error.empty();
            spinner->setVisibility(loading ? Visibility::Visible : Visibility::Hidden);
            if (loading) {
                stateText->setText(tr("detail.loading_episodes"));
            } else if (!info) {
                stateText->setText(app.session().offline ? tr("detail.episodes_offline", {error})
                                                         : tr("detail.episodes_failed", {error, screens::keyName(A::Confirm)}));
            } else if (info->seasons.empty()) {
                stateText->setText(tr("detail.no_episodes"));
            } else {
                stateText->setText("");
            }

            buttonCount = 0;
            seriesplan::Action a = plan();
            if (info && a.kind != seriesplan::Kind::None) {
                actions[buttonCount] = Action::Resume;
                buttons[buttonCount++]->setText("\xE2\x96\xB6  " + seriesplan::label(a, *info));
            } else if (!info && !error.empty()) {
                actions[buttonCount] = Action::Retry;
                buttons[buttonCount++]->setText(tr("common.try_again"));
            }
            bool fav = app.library().isFavorite(ContentType::Series, series.seriesId);
            actions[buttonCount] = Action::Favorite;
            buttons[buttonCount++]->setText(fav ? "\xE2\x98\x85  " + tr("favorites.in_favorites")
                                                : "\xE2\x98\x86  " + tr("favorites.add"));
            buttonFocus = std::min(buttonFocus, buttonCount - 1);
            if (focus != 0 && !hasSeasons()) {
                focus = 0;
            }
            episodes->reload();
            refreshFocus();
        }

        Series series;
        screens::DetailArt art;
        std::shared_ptr<const SeriesInfo> info;
        ui::Backdrop *backdrop;
        ui::PosterView *cover;
        ui::Label *title;
        ui::Label *meta;
        ui::Label *plot;
        ui::Button *buttons[2];
        Action actions[2] = {Action::Resume, Action::Favorite};
        RectangleShape *seasonLayer;
        ui::ListView *episodes;
        ui::Label *stateText;
        ui::Spinner *spinner;
        ui::HintBar *hints;
        std::vector<EpisodeRow> rows;
        std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);
        std::string returnEpisode;
        int seasonIndex = 0;
        int focus = 0;
        int buttonFocus = 0;
        int buttonCount = 0;
        unsigned vodGen = 0;
        unsigned imageGen = 0;
        unsigned libraryGen = 0;
        unsigned downloadsGen = 0;
        double lastDownloadRefresh = 0;
        dl::LiveProgress liveDownload;
    };
}

namespace screens {
    Screen *makeMovieDetail(App &app, const Movie &movie) {
        return new MovieDetailScreen(app, movie);
    }

    Screen *makeSeriesDetail(App &app, const Series &series) {
        return new SeriesDetailScreen(app, series);
    }
}
