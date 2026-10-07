// Movie and Series detail screens.
//
// Both open at once with what the catalog already has (title, year, rating, poster; for series also plot,
// cast, genre) and request their detail response lazily (get_vod_info / get_series_info). The large poster
// is loaded as its own image kind; the grid poster (usually cached already) is shown until it arrives.

#include <cctype>

#include "common.h"
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

    // poster: the large version when ready, else the grid version, else the title
    void bindPoster(App &app, ui::PosterView *view, const std::string &title, const std::string &url) {
        std::shared_ptr<ImageSet> big = app.images().get(ImageKind::PosterLarge, url);
        std::shared_ptr<ImageSet> small = big ? nullptr : app.images().get(ImageKind::Poster, url);
        const std::shared_ptr<ImageSet> &img = big ? big : small;
        view->set(title, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
    }

    // ------------------------------------------------------------------ movie

    class MovieDetailScreen : public Screen {
    public:
        MovieDetailScreen(App &a, Movie m) : Screen(a), movie(std::move(m)) {
            ui::background(this);
            poster = new ui::PosterView(FloatRect(theme::SAFE_X, 120, 380, 570), theme::HEADING);
            add(poster);
            const float x = theme::SAFE_X + 380 + 64;
            const float w = theme::SCREEN_W - theme::SAFE_X - x;
            title = ui::label(this, movie.title, theme::TITLE, x, 120, ui::Weight::SemiBold);
            title->setMaxWidth(w);
            title->setMaxLines(2);
            meta = ui::label(this, "", theme::BODY, x, 0, ui::Weight::Regular, theme::textDim());
            meta->setMaxWidth(w);
            tech = ui::label(this, "", theme::LABEL, x, 0, ui::Weight::Regular, theme::textMuted());
            tech->setMaxWidth(w);
            credits = ui::label(this, "", theme::LABEL, x, 0, ui::Weight::Regular, theme::textDim());
            credits->setMaxWidth(w);
            credits->setMaxLines(4);
            plot = ui::label(this, "", theme::BODY, x, 0, ui::Weight::Regular, theme::text());
            plot->setMaxWidth(w);
            plot->setMaxLines(6);
            progressText = ui::label(this, "", theme::LABEL, x, 760, ui::Weight::SemiBold, theme::textDim());
            barTrack = ui::box(this, FloatRect(x, 800, 600, 8), Color(255, 255, 255, 50), 4);
            barFill = ui::box(barTrack, FloatRect(0, 0, 8, 8), theme::accent(), 4);
            for (int i = 0; i < 3; i++) {
                buttons[i] = new ui::Button("", FloatRect(x + (float) i * 360, 850, 340, 84), i == 0);
                add(buttons[i]);
            }
            screens::hintBar(this, {{ui::Glyph::Cross, "Select"}, {ui::Glyph::Square, "Favorite"},
                                    {ui::Glyph::Circle, "Back"}});
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

        void tick(double) override {
            if (app.vod().generation() != vodGen || app.images().generation() != imageGen
                || app.library().generation() != libraryGen) {
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
            ResetProgress
        };

        void requestImages() {
            if (app.settings().get().loadImages) {
                app.images().want({{ImageKind::PosterLarge, movie.icon}, {ImageKind::Poster, movie.icon}});
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
            return it;
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
                    app.toast(on ? "Added to favorites" : "Removed from favorites", on ? ToastKind::Success
                                                                                        : ToastKind::Info);
                    break;
                }
                case Action::ResetProgress:
                    app.library().resetProgress(ContentType::Movie, movie.streamId);
                    app.saveLibrary();
                    app.toast("Marked as not watched");
                    break;
            }
            refresh();
        }

        void refresh() {
            vodGen = app.vod().generation();
            imageGen = app.images().generation();
            libraryGen = app.library().generation();
            bindPoster(app, poster, movie.title, movie.icon);
            std::shared_ptr<const MovieInfo> info = app.vod().movieInfo(movie.streamId);
            std::string error = app.vod().detailError("m" + movie.streamId);
            float rating = info && info->rating > 0 ? info->rating : movie.rating;
            int duration = info ? info->durationSeconds : 0;
            std::string res = info ? fmt::resolution(info->media.width, info->media.height) : "";
            meta->setText(join({movie.year > 0 ? std::to_string(movie.year) : "", fmt::rating(rating),
                                fmt::duration(duration), res}));
            tech->setText(info ? mediaLine(info->media, movie.extension) : "");
            std::string c;
            if (info) {
                if (!info->genre.empty()) {
                    c += info->genre + "\n";
                }
                if (!info->director.empty()) {
                    c += "Director: " + info->director + "\n";
                }
                if (!info->cast.empty()) {
                    c += "Cast: " + info->cast;
                }
            }
            credits->setText(c);
            if (info) {
                plot->setText(info->plot.empty() ? "" : info->plot);
                plot->setColor(theme::text());
            } else {
                plot->setText(error.empty() ? "Loading details" "\xE2\x80\xA6"
                                            : "Details unavailable (" + error + "). You can still play the movie.");
                plot->setColor(theme::textMuted());
            }
            // stack the text blocks under the title
            float y = 120 + title->height() + 16;
            meta->setPosition(meta->getPosition().x, y);
            y += meta->height() + 10;
            tech->setPosition(tech->getPosition().x, y);
            y += tech->getText().empty() ? 0 : tech->height() + 24;
            credits->setPosition(credits->getPosition().x, y);
            y += credits->getText().empty() ? 0 : credits->height() + 20;
            plot->setPosition(plot->getPosition().x, y);

            const HistoryEntry *p = app.library().progressOf(ContentType::Movie, movie.streamId);
            bool resumable = p && progress::inProgress(p->position, p->duration, p->watched);
            bool watched = p && p->watched;
            if (resumable) {
                progressText->setText(fmt::remaining(p->position, p->duration) + "  (stopped at "
                                      + fmt::clock(p->position) + ")");
            } else {
                progressText->setText(watched ? "\xE2\x9C\x93  Watched" : "");
            }
            progressText->setColor(watched ? theme::success() : theme::textDim());
            barTrack->setVisibility(resumable ? Visibility::Visible : Visibility::Hidden);
            if (resumable) {
                barFill->setSize(std::max(8.0f, 600.0f * (float) progress::fraction(p->position, p->duration)), 8);
            }

            bool fav = app.library().isFavorite(ContentType::Movie, movie.streamId);
            buttonCount = 0;
            auto addButton = [this](Action a, const std::string &text) {
                actions[buttonCount] = a;
                buttons[buttonCount]->setText(text);
                buttonCount++;
            };
            if (resumable) {
                addButton(Action::Resume, "\xE2\x96\xB6  Resume " + fmt::clock(p->position));   // exactly there
                addButton(Action::StartOver, "Start over");
            } else {
                addButton(Action::Play, watched ? "\xE2\x96\xB6  Play again" : "\xE2\x96\xB6  Play");
                if (watched) {
                    addButton(Action::ResetProgress, "Mark as not watched");
                }
            }
            addButton(Action::Favorite, fav ? "\xE2\x98\x85  In favorites" : "\xE2\x98\x86  Add to favorites");
            focus = std::min(focus, buttonCount - 1);
            refreshButtons();
        }

        void refreshButtons() {
            for (int i = 0; i < 3; i++) {
                buttons[i]->setVisibility(i < buttonCount ? Visibility::Visible : Visibility::Hidden);
                buttons[i]->setFocused(i == focus);
            }
        }

        Movie movie;
        ui::PosterView *poster;
        ui::Label *title;
        ui::Label *meta;
        ui::Label *tech;
        ui::Label *credits;
        ui::Label *plot;
        ui::Label *progressText;
        RectangleShape *barTrack;
        RectangleShape *barFill;
        ui::Button *buttons[3];
        Action actions[3] = {Action::Play, Action::Favorite, Action::Favorite};
        int buttonCount = 0;
        int focus = 0;
        bool backFromPlayback = false;
        unsigned vodGen = 0;
        unsigned imageGen = 0;
        unsigned libraryGen = 0;
    };

    // ------------------------------------------------------------------ series

    const float S_TOP = 110;
    const float S_COVER_W = 240;
    const float S_COVER_H = 360;
    const float S_TEXT_X = theme::SAFE_X + S_COVER_W + 48;
    const float S_SEASONS_Y = 500;
    const float S_EPISODES_Y = 576;
    const float S_EPISODES_H = 404;
    const float CHIP_W = 180;
    const float CHIP_H = 56;

    class SeriesDetailScreen : public Screen, public ui::ListView::Adapter {
    public:
        SeriesDetailScreen(App &a, Series s) : Screen(a), series(std::move(s)) {
            ui::background(this);
            cover = new ui::PosterView(FloatRect(theme::SAFE_X, S_TOP, S_COVER_W, S_COVER_H), theme::BODY);
            add(cover);
            const float w = theme::SCREEN_W - theme::SAFE_X - S_TEXT_X;
            title = ui::label(this, series.title, theme::TITLE, S_TEXT_X, S_TOP, ui::Weight::SemiBold);
            title->setMaxWidth(w);
            meta = ui::label(this, "", theme::BODY, S_TEXT_X, 0, ui::Weight::Regular, theme::textDim());
            meta->setMaxWidth(w);
            credits = ui::label(this, "", theme::LABEL, S_TEXT_X, 0, ui::Weight::Regular, theme::textMuted());
            credits->setMaxWidth(w);
            credits->setMaxLines(2);
            plot = ui::label(this, "", theme::LABEL, S_TEXT_X, 0, ui::Weight::Regular, theme::textDim());
            plot->setMaxWidth(w);
            plot->setMaxLines(3);
            for (int i = 0; i < 2; i++) {
                // the main button holds "Resume S01E03 · 1:02:15"
                buttons[i] = new ui::Button("", FloatRect(S_TEXT_X + (float) i * 520, S_TOP + S_COVER_H - 76,
                                                          i == 0 ? 500.0f : 400.0f, 76), i == 0);
                add(buttons[i]);
            }
            seasonLayer = new RectangleShape(FloatRect(theme::SAFE_X, S_SEASONS_Y, theme::SCREEN_W - 2 * theme::SAFE_X,
                                                       CHIP_H));
            seasonLayer->setFillColor(Color::Transparent);
            add(seasonLayer);
            episodes = new ui::ListView(FloatRect(theme::SAFE_X, S_EPISODES_Y, theme::SCREEN_W - 2 * theme::SAFE_X,
                                                  S_EPISODES_H), 92, 8, this);
            add(episodes);
            stateText = ui::label(this, "", theme::BODY, theme::SAFE_X, S_EPISODES_Y + 120, ui::Weight::Regular,
                                  theme::textMuted());
            stateText->setAlign(ui::Align::Center, theme::SCREEN_W - 2 * theme::SAFE_X);
            stateText->setMaxWidth(1400);
            stateText->setMaxLines(2);
            spinner = new ui::Spinner(18);
            spinner->setPosition((theme::SCREEN_W - 18 * 3.6f) / 2, S_EPISODES_Y + 200);
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
            if (app.images().generation() != imageGen) {
                imageGen = app.images().generation();
                bindPoster(app, cover, series.title, series.cover);
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
            r.bg = ui::box(nullptr, FloatRect(0, 0, w, h), theme::surface(), theme::RADIUS_SMALL);
            r.code = ui::label(r.bg, "", theme::LABEL, 28, ui::Label::centerOffset(theme::LABEL, h) - 14,
                               ui::Weight::SemiBold, theme::accent());
            r.name = ui::label(r.bg, "", theme::BODY, 170, ui::Label::centerOffset(theme::BODY, h) - 14);
            r.name->setMaxWidth(w - 170 - 320);
            r.detail = ui::label(r.bg, "", theme::CAPTION, 170, ui::Label::centerOffset(theme::BODY, h) + 22,
                                 ui::Weight::Regular, theme::textMuted());
            r.detail->setMaxWidth(w - 170 - 320);
            r.state = ui::label(r.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h), ui::Weight::SemiBold,
                                theme::success());
            r.state->setAlign(ui::Align::Right, w - 28);
            // in progress: "24:16 / 57:00" above a bar
            r.time = ui::label(r.bg, "", theme::CAPTION, 0, ui::Label::centerOffset(theme::CAPTION, h) - 14,
                               ui::Weight::SemiBold, theme::textDim());
            r.time->setAlign(ui::Align::Right, w - 28);
            r.barTrack = ui::box(r.bg, FloatRect(w - 28 - 240, h / 2 + 14, 240, 6), Color(255, 255, 255, 50), 3);
            r.barFill = ui::box(r.barTrack, FloatRect(0, 0, 6, 6), theme::accent(), 3);
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
                r.code->setText(fmt::episodeCode(ep.season, ep.number));
                r.name->setText(ep.title.empty() ? "Episode " + std::to_string(ep.number) : ep.title);
                r.name->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                r.name->setColor(focused ? Color::White : theme::text());
                r.detail->setText(join({fmt::duration(ep.durationSeconds),
                                        fmt::resolution(ep.media.width, ep.media.height)}));
                r.state->setText(state == seriesplan::EpisodeState::Watched ? "\xE2\x9C\x93  Watched" : "");
                double total = p && p->duration > 0 ? p->duration : ep.durationSeconds;
                r.time->setText(partial ? (total > 0 ? fmt::clock(p->position) + " / " + fmt::clock(total)
                                                     : fmt::clock(p->position)) : "");
                r.barTrack->setVisibility(partial ? Visibility::Visible : Visibility::Hidden);
                if (partial) {
                    r.barFill->setSize(std::max(6.0f, 240.0f * (float) progress::fraction(p->position, total)), 6);
                }
                r.bg->setFillColor(focused ? theme::rowFocus() : selected ? theme::surfaceRaised() : theme::surface());
                r.bg->setOutlineColor(theme::accent());
                r.bg->setOutlineThickness(focused ? 3 : 0);
            }
        }

        // ------------------------------------------------------------------ input
        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    if (focus == 2 && episodes->selected() > 0) {
                        episodes->moveSelection(-1);
                    } else if (focus == 2) {
                        setFocus(hasSeasons() ? 1 : 0);
                    } else if (focus == 1) {
                        setFocus(0);
                    }
                    break;
                case PadButton::Down:
                    if (focus == 0) {
                        setFocus(hasSeasons() ? 1 : focus);
                    } else if (focus == 1 && count() > 0) {
                        setFocus(2);
                    } else if (focus == 2) {
                        episodes->moveSelection(1);
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
                        activate(Action::Favorite);
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
            RectangleShape *bg;
            ui::Label *code;
            ui::Label *name;
            ui::Label *detail;
            ui::Label *state;
            ui::Label *time;
            RectangleShape *barTrack;
            RectangleShape *barFill;
        };

        bool hasSeasons() const { return info && !info->seasons.empty(); }

        void requestImages() {
            if (app.settings().get().loadImages) {
                app.images().want({{ImageKind::PosterLarge, series.cover}, {ImageKind::Poster, series.cover}});
            }
        }

        // every episode of every season, in order: the player's "next episode" queue
        std::vector<VodItem> queue(int &position, int season, int episode) const {
            std::vector<VodItem> q;
            position = 0;
            for (size_t s = 0; s < info->seasons.size(); s++) {
                for (size_t e = 0; e < info->seasons[s].episodes.size(); e++) {
                    const Episode &ep = info->seasons[s].episodes[e];
                    if ((int) s == season && (int) e == episode) {
                        position = (int) q.size();
                    }
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
                    q.push_back(it);
                }
            }
            return q;
        }

        void playEpisode(int season, int episode, bool resume) {
            int position = 0;
            std::vector<VodItem> q = queue(position, season, episode);
            std::weak_ptr<bool> alive = aliveToken;
            app.push(screens::makeVodPlayer(app, q, position, resume, [this, alive](const std::string &id) {
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
                    app.toast(on ? "Added to favorites" : "Removed from favorites", on ? ToastKind::Success
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

        void refreshFocus() {
            for (int i = 0; i < 2; i++) {
                buttons[i]->setVisibility(i < buttonCount ? Visibility::Visible : Visibility::Hidden);
                buttons[i]->setFocused(focus == 0 && i == buttonFocus);
            }
            episodes->setFocused(focus == 2);
            buildChips();
            if (focus == 2) {
                hints->setHints({{ui::Glyph::Cross, "Play"}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, "Season"},
                                 {ui::Glyph::L2, ""}, {ui::Glyph::R2, "Page"}, {ui::Glyph::Square, "Favorite"},
                                 {ui::Glyph::Circle, "Back"}});
            } else {
                hints->setHints({{ui::Glyph::Cross, "Select"}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, "Season"},
                                 {ui::Glyph::Square, "Favorite"}, {ui::Glyph::Circle, "Back"}});
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
            const int fit = (int) ((seasonLayer->getSize().x + 16) / (CHIP_W + 16));
            int first = std::max(0, std::min(seasonIndex - fit / 2, n - fit));
            for (int i = first; i < n && i < first + fit; i++) {
                const Season &s = info->seasons[(size_t) i];
                bool sel = i == seasonIndex;
                bool foc = sel && focus == 1;
                auto *chip = ui::box(seasonLayer, FloatRect((float) (i - first) * (CHIP_W + 16), 0, CHIP_W, CHIP_H),
                                     foc ? theme::accent() : sel ? theme::surfaceRaised() : theme::surface(),
                                     CHIP_H / 2);
                chip->setOutlineColor(theme::withAlpha(Color::White, 220));
                chip->setOutlineThickness(foc ? 3 : 0);
                auto *l = ui::label(chip, s.number > 0 ? "Season " + std::to_string(s.number) : "Specials",
                                    theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, CHIP_H),
                                    sel ? ui::Weight::SemiBold : ui::Weight::Regular,
                                    foc ? Color::White : sel ? theme::text() : theme::textDim());
                l->setAlign(ui::Align::Center, CHIP_W);
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
            }
            imageGen = app.images().generation();
            bindPoster(app, cover, series.title, series.cover);
            meta->setText(join({series.year > 0 ? std::to_string(series.year) : "", fmt::rating(series.rating),
                                series.genre,
                                series.runtimeMinutes > 0 ? std::to_string(series.runtimeMinutes) + " min per episode" : "",
                                hasSeasons() ? std::to_string(info->seasons.size())
                                               + (info->seasons.size() == 1 ? " season" : " seasons") : ""}));
            credits->setText(join({series.director.empty() ? "" : "Director: " + series.director,
                                   series.cast.empty() ? "" : "Cast: " + series.cast}));
            plot->setText(series.plot);
            float y = S_TOP + title->height() + 12;
            meta->setPosition(S_TEXT_X, y);
            y += meta->height() + 10;
            credits->setPosition(S_TEXT_X, y);
            y += credits->getText().empty() ? 0 : credits->height() + 10;
            plot->setPosition(S_TEXT_X, y);

            std::string error = app.vod().detailError("s" + series.seriesId);
            bool loading = !info && error.empty();
            spinner->setVisibility(loading ? Visibility::Visible : Visibility::Hidden);
            if (loading) {
                stateText->setText("Loading episodes" "\xE2\x80\xA6");
            } else if (!info) {
                stateText->setText("Episodes could not be loaded (" + error + "). Press X to try again.");
            } else if (info->seasons.empty()) {
                stateText->setText("The provider lists no episodes for this series.");
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
                buttons[buttonCount++]->setText("Try again");
            }
            bool fav = app.library().isFavorite(ContentType::Series, series.seriesId);
            actions[buttonCount] = Action::Favorite;
            buttons[buttonCount++]->setText(fav ? "\xE2\x98\x85  In favorites" : "\xE2\x98\x86  Add to favorites");
            buttonFocus = std::min(buttonFocus, buttonCount - 1);
            if (focus != 0 && !hasSeasons()) {
                focus = 0;
            }
            episodes->reload();
            refreshFocus();
        }

        Series series;
        std::shared_ptr<const SeriesInfo> info;
        ui::PosterView *cover;
        ui::Label *title;
        ui::Label *meta;
        ui::Label *credits;
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
