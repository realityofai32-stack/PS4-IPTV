// Home: section tiles, Continue Watching (resume with one press) and Recently Watched.
// Movies / Series counts appear once their catalog has been loaded (it is never loaded at sign-in).
//
// Continue Watching comes from the progress store (LibraryStore::continueWatching): movies and episodes in
// progress, most recent playback first, one card per series. It is rebuilt whenever the store changes
// (generation), so returning from the player shows the new position at once. Live TV never appears there;
// Recently Watched shows at most RECENT_LIVE_MAX channels while movies/episodes can fill the row.

#include "common.h"
#include "../core/format.h"
#include "../platform/clock.h"

using namespace c2d;
using namespace iptv;
using screens::VodItem;

namespace {

    const float TILE_Y = 150;
    const float TILE_H = 200;
    const float CW_Y = 384;
    const float CW_CARD_W = 144;
    const float CW_POSTER_H = 216;
    const float CW_GAP = 24;
    const int CW_MAX = 10;
    const float RECENT_Y = 756;
    const float RECENT_W = 330;
    const float RECENT_H = 104;
    const int RECENT_MAX = 5;
    const int RECENT_LIVE_MAX = 3;

    class HomeScreen : public Screen {
    public:
        explicit HomeScreen(App &a) : Screen(a) {
            ui::background(this);
            const Session &s = app.session();

            // top bar
            ui::label(this, "PS4 IPTV", theme::HEADING, theme::SAFE_X, theme::SAFE_Y, ui::Weight::SemiBold,
                      theme::accent());
            clock = ui::label(this, clockx::localTime(), theme::HEADING, 0, theme::SAFE_Y, ui::Weight::SemiBold);
            clock->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);
            const float whoRight = theme::SCREEN_W - theme::SAFE_X - 180;
            auto *who = ui::label(this, s.profile.name, theme::BODY, 0, theme::SAFE_Y + 4, ui::Weight::SemiBold,
                                  theme::textDim());
            who->setMaxWidth(480);
            who->setAlign(ui::Align::Right, whoRight);
            auto *dot = new CircleShape(8);
            dot->setPointCount(16);
            dot->setFillColor(s.connected ? theme::success() : theme::danger());
            dot->setPosition(whoRight - who->width() - 28, theme::SAFE_Y + 15);
            add(dot);

            // account notice
            std::string notice;
            int64_t now = clockx::unixNow();
            if (s.httpsWarning) {
                notice = "This server uses HTTPS. This build currently supports HTTP streams only.";
            } else if (s.account.expiresAt > 0 && s.account.expiresAt - now < 7 * 86400) {
                notice = "Your subscription expires on " + clockx::localDate(s.account.expiresAt) + ".";
            }
            if (!notice.empty()) {
                auto *n = ui::label(this, notice, theme::LABEL, theme::SAFE_X, 112, ui::Weight::Regular,
                                    theme::warning());
                n->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X);
            }

            const char *titles[] = {"Live TV", "Movies", "Series", "Favorites", "Search", "Settings"};
            const float gap = 24;
            const float tileW = (theme::SCREEN_W - 2 * theme::SAFE_X - 5 * gap) / 6;
            for (int i = 0; i < 6; i++) {
                Tile &t = tiles[i];
                t.bg = ui::box(this, FloatRect(theme::SAFE_X + (float) i * (tileW + gap), TILE_Y, tileW, TILE_H),
                               theme::surface(), 20);
                t.accentBar = ui::box(t.bg, FloatRect(28, 30, 44, 6), theme::accent(), 3);
                t.title = ui::label(t.bg, titles[i], theme::HEADING, 28, 96, ui::Weight::SemiBold);
                t.title->setMaxWidth(tileW - 56);
                t.subtitle = ui::label(t.bg, "", theme::LABEL, 28, 148, ui::Weight::Regular, theme::textDim());
                t.subtitle->setMaxWidth(tileW - 56);
            }

            auto *cwHeading = ui::label(this, "Continue Watching", theme::HEADING, theme::SAFE_X, CW_Y,
                                        ui::Weight::SemiBold);
            // the focused card in full: "Series  ·  S01E03 · Title  ·  24:16 / 57:00"
            cwDetail = ui::label(this, "", theme::LABEL, theme::SAFE_X + cwHeading->width() + 32, CW_Y + 8,
                                 ui::Weight::Regular, theme::textDim());
            cwDetail->setMaxWidth(theme::SCREEN_W - theme::SAFE_X - (theme::SAFE_X + cwHeading->width() + 32));
            cwLayer = new RectangleShape(FloatRect(theme::SAFE_X, CW_Y + 56, theme::SCREEN_W - 2 * theme::SAFE_X, 300));
            cwLayer->setFillColor(Color::Transparent);
            add(cwLayer);
            cwEmpty = ui::label(this, "Movies and episodes you start appear here, ready to resume.", theme::BODY,
                                theme::SAFE_X + 8, CW_Y + 140, ui::Weight::Regular, theme::textMuted());

            ui::label(this, "Recently Watched", theme::HEADING, theme::SAFE_X, RECENT_Y, ui::Weight::SemiBold);
            recentLayer = new RectangleShape(FloatRect(theme::SAFE_X, RECENT_Y + 56, theme::SCREEN_W - 2 * theme::SAFE_X,
                                                       RECENT_H));
            recentLayer->setFillColor(Color::Transparent);
            add(recentLayer);
            recentEmpty = ui::label(this, "Channels, movies and episodes you watch appear here.", theme::BODY,
                                    theme::SAFE_X + 8, RECENT_Y + 90, ui::Weight::Regular, theme::textMuted());

            hints = screens::hintBar(this, {{ui::Glyph::Cross, "Open"}, {ui::Glyph::Triangle, "Search"},
                                            {ui::Glyph::Options, "Settings"}, {ui::Glyph::Circle, "Exit"}});
            rebuildRows();
            refresh();
        }

        const char *name() const override { return "home"; }

        void onResume() override {
            rebuildRows();
            refresh();
            requestImages();
        }

        void onEnter() override {
            requestImages();
        }

        void onPause() override {
            app.images().want(std::vector<ImageRequest>());
        }

        void tick(double) override {
            std::string t = clockx::localTime();
            if (t != clock->getText()) {
                clock->setText(t);
                redraw();
            }
            if (app.vod().generation() != vodGen || app.images().generation() != imageGen
                || app.library().generation() != libraryGen) {
                imageGen = app.images().generation();
                rebuildRows();
                refresh();
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Left:
                case PadButton::L1:
                    if (index[zone] > 0) {
                        index[zone]--;
                    }
                    break;
                case PadButton::Right:
                case PadButton::R1:
                    if (index[zone] + 1 < zoneSize(zone)) {
                        index[zone]++;
                    }
                    break;
                case PadButton::Down:
                    for (int z = zone + 1; z < 3; z++) {
                        if (zoneSize(z) > 0) {
                            zone = z;
                            index[z] = std::min(index[z], zoneSize(z) - 1);
                            break;
                        }
                    }
                    break;
                case PadButton::Up:
                    for (int z = zone - 1; z >= 0; z--) {
                        if (zoneSize(z) > 0) {
                            zone = z;
                            break;
                        }
                    }
                    break;
                case PadButton::Cross:
                    if (!e.repeat) {
                        activate();
                    }
                    return;
                case PadButton::Triangle:
                    if (!e.repeat) {
                        openTile(4);
                    }
                    return;
                case PadButton::Options:
                    if (!e.repeat) {
                        openTile(5);
                    }
                    return;
                case PadButton::Circle:
                    if (e.repeat) {
                        return;
                    }
                    if (zone != 0) {
                        zone = 0;
                        break;
                    }
                    app.push(screens::makeDialog(app, "Exit PS4 IPTV?", "", {"Cancel", "Exit"}, [this](int c) {
                        if (c == 1) {
                            app.quit();
                        }
                    }));
                    return;
                default:
                    return;
            }
            refresh();
        }

    private:
        struct Tile {
            RectangleShape *bg;
            RectangleShape *accentBar;
            ui::Label *title;
            ui::Label *subtitle;
        };

        struct Card {
            RectangleShape *bg;          // focus frame
            ui::PosterView *poster;      // Continue Watching
            ui::LogoView *logo;          // Recently Watched
        };

        int zoneSize(int z) const {
            return z == 0 ? 6 : z == 1 ? (int) cw.size() : (int) recent.size();
        }

        static std::string sectionCount(const SectionStatus &st, size_t n, const char *noun) {
            if (n > 0) {
                return std::to_string(n) + " " + noun;
            }
            switch (st.status) {
                case CatalogStatus::Loading:
                    return "Loading" "\xE2\x80\xA6";
                case CatalogStatus::Failed:
                    return "Unavailable, open to retry";
                default:
                    return "Open to load";
            }
        }

        // one line under a history card: episode code or the type
        static std::string kindLine(const HistoryEntry &h) {
            if (h.type == ContentType::Series) {
                return fmt::episodeCode(h.season, h.episode);
            }
            return h.type == ContentType::Movie ? "Movie" : "Live TV";
        }

        static std::string titleOf(const HistoryEntry &h) {
            return h.type == ContentType::Series && !h.seriesName.empty() ? h.seriesName : h.name;
        }

        // "S01E03 · Title" for an episode
        static std::string episodeLine(const HistoryEntry &h) {
            std::string code = fmt::episodeCode(h.season, h.episode);
            return h.name.empty() ? code : code + " \xC2\xB7 " + h.name;
        }

        static std::string cwDetailOf(const HistoryEntry &h) {
            std::string sep = "   \xC2\xB7   ";
            std::string time = h.duration > 0 ? fmt::clock(h.position) + " / " + fmt::clock(h.duration)
                                               : fmt::clock(h.position);
            std::string left = fmt::remaining(h.position, h.duration);
            if (h.type == ContentType::Series) {
                return titleOf(h) + sep + episodeLine(h) + sep + time + (left.empty() ? "" : sep + left);
            }
            return h.name + sep + time + (left.empty() ? "" : sep + left);
        }

        void rebuildRows() {
            vodGen = app.vod().generation();
            libraryGen = app.library().generation();
            cw.clear();
            for (const HistoryEntry *h: app.library().continueWatching(CW_MAX)) {
                cw.push_back(*h);
            }
            recent.clear();
            for (const HistoryEntry *h: app.library().recentlyWatched(RECENT_MAX, RECENT_LIVE_MAX)) {
                recent.push_back(*h);
            }
            for (auto *layer: {cwLayer, recentLayer}) {
                for (auto *c: layer->getChilds()) {
                    layer->remove(c);
                    delete c;
                }
            }
            cwCards.clear();
            recentCards.clear();
            for (size_t i = 0; i < cw.size(); i++) {
                const HistoryEntry &h = cw[i];
                float x = (float) i * (CW_CARD_W + CW_GAP);
                Card c{};
                c.bg = ui::box(cwLayer, FloatRect(x - 6, -6, CW_CARD_W + 12, CW_POSTER_H + 12), Color::Transparent, 14);
                c.poster = new ui::PosterView(FloatRect(x, 0, CW_CARD_W, CW_POSTER_H), theme::CAPTION);
                cwLayer->add(c.poster);
                std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Poster, h.icon);
                c.poster->set(titleOf(h), img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                c.poster->setProgress(std::max(0.02f, (float) progress::fraction(h.position, h.duration)));
                auto *t = ui::label(cwLayer, titleOf(h), theme::CAPTION, x, CW_POSTER_H + 10, ui::Weight::SemiBold);
                t->setMaxWidth(CW_CARD_W);
                // episodes: "S01E03 · Title"; movies: time left
                std::string second = h.type == ContentType::Series ? episodeLine(h) : fmt::remaining(h.position, h.duration);
                auto *k = ui::label(cwLayer, second, theme::CAPTION, x, CW_POSTER_H + 38, ui::Weight::Regular,
                                    theme::textDim());
                k->setMaxWidth(CW_CARD_W);
                cwCards.push_back(c);
            }
            for (size_t i = 0; i < recent.size(); i++) {
                const HistoryEntry &h = recent[i];
                float x = (float) i * (RECENT_W + 20);
                Card c{};
                c.bg = ui::box(recentLayer, FloatRect(x, 0, RECENT_W, RECENT_H), theme::surface(), theme::RADIUS_SMALL);
                bool live = h.type == ContentType::Live;
                std::shared_ptr<ImageSet> img = app.images().get(live ? ImageKind::Logo : ImageKind::Poster, h.icon);
                if (live) {
                    c.logo = new ui::LogoView(FloatRect(14, (RECENT_H - 60) / 2, 104, 60), 22, 4, 1.25f);
                    c.logo->set(h.name, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                    c.bg->add(c.logo);
                } else {
                    c.poster = new ui::PosterView(FloatRect(14, 8, 59, 88), 14);
                    c.poster->set(titleOf(h), img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                    c.bg->add(c.poster);
                }
                float tx = live ? 134 : 90;
                auto *t = ui::label(c.bg, titleOf(h), theme::LABEL, tx, 22, ui::Weight::SemiBold);
                t->setMaxWidth(RECENT_W - tx - 16);
                auto *k = ui::label(c.bg, kindLine(h), theme::CAPTION, tx, 58, ui::Weight::Regular, theme::textDim());
                k->setMaxWidth(RECENT_W - tx - 16);
                recentCards.push_back(c);
            }
            cwEmpty->setVisibility(cw.empty() ? Visibility::Visible : Visibility::Hidden);
            recentEmpty->setVisibility(recent.empty() ? Visibility::Visible : Visibility::Hidden);
            for (int z = 1; z < 3; z++) {
                index[z] = std::min(index[z], std::max(0, zoneSize(z) - 1));
            }
            if (zoneSize(zone) == 0) {
                zone = 0;
            }
        }

        void requestImages() {
            if (!app.settings().get().loadImages) {
                return;
            }
            std::vector<ImageRequest> req;
            for (const auto &h: cw) {
                req.push_back({ImageKind::Poster, h.icon});
            }
            for (const auto &h: recent) {
                req.push_back({h.type == ContentType::Live ? ImageKind::Logo : ImageKind::Poster, h.icon});
            }
            app.images().want(req);
        }

        void activate() {
            if (zone == 0) {
                openTile(index[0]);
            } else if (zone == 1) {
                resume(cw[(size_t) index[1]]);
            } else {
                openRecent(recent[(size_t) index[2]]);
            }
        }

        void openTile(int i) {
            switch (i) {
                case 0:
                    app.push(screens::makeLive(app));
                    break;
                case 1:
                    app.push(screens::makeMovies(app));
                    break;
                case 2:
                    app.push(screens::makeSeries(app));
                    break;
                case 3:
                    app.push(screens::makeFavorites(app));
                    break;
                case 4:
                    app.push(screens::makeSearch(app));
                    break;
                default:
                    app.push(screens::makeSettings(app));
                    break;
            }
        }

        static VodItem itemOf(const HistoryEntry &h) {
            VodItem it;
            it.type = h.type;
            it.id = h.id;
            it.extension = h.extension.empty() ? "mkv" : h.extension;
            it.title = h.name;
            it.image = h.icon;
            it.seriesId = h.seriesId;
            it.seriesName = h.seriesName;
            it.season = h.season;
            it.episode = h.episode;
            it.durationHint = h.duration;
            return it;
        }

        // Continue Watching: play at once. Episodes get the series' next episodes when its details are cached.
        void resume(const HistoryEntry &h) {
            std::vector<VodItem> queue = {itemOf(h)};
            int position = 0;
            if (h.type == ContentType::Series) {
                if (std::shared_ptr<const SeriesInfo> info = app.vod().seriesInfo(h.seriesId)) {
                    queue.clear();
                    for (const auto &s: info->seasons) {
                        for (const auto &ep: s.episodes) {
                            VodItem it = itemOf(h);
                            it.id = ep.id;
                            it.extension = ep.extension.empty() ? "mkv" : ep.extension;
                            it.title = ep.title;
                            it.season = ep.season;
                            it.episode = ep.number;
                            it.durationHint = ep.durationSeconds;
                            if (ep.id == h.id) {
                                position = (int) queue.size();
                            }
                            queue.push_back(it);
                        }
                    }
                    if (queue.empty()) {
                        queue.push_back(itemOf(h));
                    }
                } else {
                    app.vod().requestSeriesInfo(app.session().profile, h.seriesId);   // for the next time
                }
            }
            app.push(screens::makeVodPlayer(app, queue, position, true));
        }

        void openRecent(const HistoryEntry &h) {
            switch (h.type) {
                case ContentType::Live: {
                    const auto &live = app.session().live;
                    for (int i = 0; i < (int) live.channels().size(); i++) {
                        if (live.channels()[(size_t) i].streamId == h.id) {
                            app.push(screens::makeLivePlayer(app, {i}, 0));
                            return;
                        }
                    }
                    app.toast("This channel is no longer in the list", ToastKind::Error);
                    return;
                }
                case ContentType::Movie: {
                    const Movie *m = app.vod().movies().find(h.id);
                    if (m) {
                        app.push(screens::makeMovieDetail(app, *m));
                    } else {
                        Movie fallback;   // catalog not loaded yet: what the history knows is enough
                        fallback.streamId = h.id;
                        fallback.name = fallback.title = h.name;
                        fallback.icon = h.icon;
                        fallback.extension = h.extension;
                        app.push(screens::makeMovieDetail(app, fallback));
                    }
                    return;
                }
                default: {
                    const Series *s = app.vod().series().find(h.seriesId);
                    if (s) {
                        app.push(screens::makeSeriesDetail(app, *s));
                    } else {
                        Series fallback;
                        fallback.seriesId = h.seriesId;
                        fallback.name = fallback.title = h.seriesName;
                        fallback.cover = h.icon;
                        app.push(screens::makeSeriesDetail(app, fallback));
                    }
                    return;
                }
            }
        }

        void refresh() {
            const Session &s = app.session();
            std::string subs[6] = {
                    s.liveLoaded ? std::to_string(s.live.channels().size()) + " channels" : "Unavailable",
                    sectionCount(app.vod().movieStatus(), app.vod().movies().size(), "movies"),
                    sectionCount(app.vod().seriesStatus(), app.vod().series().size(), "series"),
                    "Channels, movies, series",
                    "Channels, movies, series",
                    "Profiles and playback"};
            for (int i = 0; i < 6; i++) {
                bool f = zone == 0 && i == index[0];
                Tile &t = tiles[i];
                t.subtitle->setText(subs[i]);
                t.bg->setFillColor(f ? theme::accentDark() : theme::surface());
                t.bg->setOutlineColor(theme::withAlpha(Color::White, 220));
                t.bg->setOutlineThickness(f ? theme::FOCUS_BORDER : 0);
                t.accentBar->setFillColor(f ? Color::White : theme::accent());
                t.subtitle->setColor(f ? theme::text() : theme::textDim());
            }
            for (size_t i = 0; i < cwCards.size(); i++) {
                cwCards[i].poster->setFocused(zone == 1 && (int) i == index[1]);
            }
            cwDetail->setText(zone == 1 && index[1] < (int) cw.size() ? cwDetailOf(cw[(size_t) index[1]]) : "");
            for (size_t i = 0; i < recentCards.size(); i++) {
                bool f = zone == 2 && (int) i == index[2];
                recentCards[i].bg->setFillColor(f ? theme::rowFocus() : theme::surface());
                recentCards[i].bg->setOutlineColor(theme::accent());
                recentCards[i].bg->setOutlineThickness(f ? 3 : 0);
            }
            hints->setHints({{ui::Glyph::Cross, zone == 1 ? "Resume" : "Open"}, {ui::Glyph::Triangle, "Search"},
                             {ui::Glyph::Options, "Settings"}, {ui::Glyph::Circle, zone == 0 ? "Exit" : "Back"}});
        }

        Tile tiles[6];
        RectangleShape *cwLayer;
        RectangleShape *recentLayer;
        ui::Label *cwEmpty;
        ui::Label *cwDetail;
        ui::Label *recentEmpty;
        ui::Label *clock;
        ui::HintBar *hints;
        std::vector<HistoryEntry> cw;
        std::vector<HistoryEntry> recent;
        std::vector<Card> cwCards;
        std::vector<Card> recentCards;
        int zone = 0;
        int index[3] = {0, 0, 0};
        unsigned vodGen = 0;
        unsigned imageGen = 0;
        unsigned libraryGen = 0;
    };

    class SectionScreen : public Screen {
    public:
        SectionScreen(App &a, const std::string &title, const std::string &message) : Screen(a) {
            ui::background(this);
            screens::header(this, title);
            auto *m = ui::label(this, message, theme::BODY, 0, 480, ui::Weight::Regular, theme::textDim());
            m->setAlign(ui::Align::Center, theme::SCREEN_W);
            m->setMaxWidth(1200);
            m->setMaxLines(3);
            screens::hintBar(this, {{ui::Glyph::Circle, "Back"}});
        }

        const char *name() const override { return "section"; }

        void handleInput(const InputEvent &e) override {
            if (e.button == PadButton::Circle || e.button == PadButton::Cross) {
                app.pop();
            }
        }
    };
}

namespace screens {
    Screen *makeHome(App &app) {
        return new HomeScreen(app);
    }

    Screen *makeSection(App &app, const std::string &title, const std::string &message) {
        return new SectionScreen(app, title, message);
    }
}
