// Home: top navigation, a hero for the most meaningful action, and content rails.
//
//   navigation   [V7 IPTV]  Home  Live TV  Movies  Series ........ (search)(favorites)(downloads)(settings)  clock
//   hero         the most recent Continue Watching item: title, episode, progress, Resume / Details, over an
//                ambient backdrop made from its poster. Without one: "What would you like to watch?" with the
//                sections as buttons.
//   rails        Continue Watching, Recently Watched, Favorites (data the app already has, nothing invented)
//
// Continue Watching comes from the progress store (LibraryStore::continueWatching): movies and episodes in
// progress, most recent playback first, one card per series. It is rebuilt whenever the store changes
// (generation), so returning from the player shows the new position at once. Live TV never appears there;
// Recently Watched shows at most RECENT_LIVE_MAX channels while movies/episodes can fill the row.
// Square on a Continue Watching card removes it from the row only (LibraryStore::dismissFromContinueWatching):
// the resume position, history, favorite and any download stay; new playback progress brings it back.
//
// Favorites shows channels (the live list is loaded at sign-in), then movies and series whose catalog is already
// in memory (catalogs load lazily when their section opens; Home never loads them), then "See all".
//
// Playlist (M3U) sources are Live TV only: no Movies / Series sections and no Continue Watching rail (playlist
// channels never have a resume position).
//
// The page scrolls vertically when a lower rail takes the focus (it glides when Smooth scrolling is on); rails
// show four cards and move by whole cards.

#include "common.h"
#include "../app/offline.h"
#include "../core/format.h"
#include "../platform/clock.h"

using namespace c2d;
using namespace iptv;
using screens::VodItem;

namespace {

    // hero
    const float HERO_TOP = 190;
    const float HERO_TEXT_W = 1000;
    const float HERO_BUTTONS_Y = 556;
    const float HERO_POSTER_W = 280;
    const float HERO_POSTER_H = 420;
    // rails
    const float RAILS_TOP = 690;
    const float RAIL_PITCH = 262;          // heading + cards + gap
    const float RAIL_HEADING_H = 52;
    const float CARD_W = 408;
    const float CARD_H = 150;
    const float CARD_GAP = 32;             // 4 x 408 + 3 x 32 = 1728 = the content width
    const int CARDS_VISIBLE = 4;
    const float VIEW_BOTTOM = 958;         // the lowest card edge kept above the hint bar
    const int CW_MAX = 10;
    const int RECENT_MAX = 10;
    const int RECENT_LIVE_MAX = 4;
    const int FAVORITES_MAX = 12;

    enum Zone {
        NAV = 0,
        HERO = 1,
        RAIL = 2   // RAIL + i: rail i
    };

    enum NavItem {
        NAV_HOME,
        NAV_LIVE,
        NAV_MOVIES,
        NAV_SERIES,
        NAV_SEARCH,
        NAV_FAVORITES,
        NAV_DOWNLOADS,
        NAV_SETTINGS
    };

    enum class RailKind {
        Continue,
        Recent,
        Favorites
    };

    enum class ItemKind {
        History,      // Continue Watching / Recently Watched entry
        Channel,      // favorite channel (index into the live catalog)
        Movie,        // favorite movie (index into the movie catalog)
        Series,       // favorite series
        SeeAll        // opens Favorites
    };

    struct RailItem {
        ItemKind kind = ItemKind::History;
        HistoryEntry history;
        int index = -1;
    };

    // one card of a rail: [artwork] title / second line / third line, and a progress bar for resumable items
    struct Card {
        RectangleShape *bg = nullptr;
        ui::PosterView *poster = nullptr;
        ui::LogoView *logo = nullptr;
        ui::ProgressBar *bar = nullptr;
        ui::Label *title = nullptr;
        std::string image;        // what the artwork shows (for rebinding when images arrive)
        bool live = false;
    };

    struct Rail {
        RailKind kind;
        RectangleShape *layer = nullptr;   // the cards, moved horizontally by whole cards
        ui::Label *heading = nullptr;
        ui::Label *detail = nullptr;       // next to the heading: the focused card in full
        ui::Label *empty = nullptr;
        std::vector<RailItem> items;
        std::vector<Card> cards;
        float y = 0;
        int first = 0;
        int index = 0;
    };

    class HomeScreen : public Screen {
    public:
        explicit HomeScreen(App &a) : Screen(a) {
            ui::background(this);
            const Session &s = app.session();
            playlist = s.profile.isPlaylist();

            // ---- scrolling page: backdrop, hero, rails
            page = new RectangleShape(FloatRect(0, 0, theme::SCREEN_W, theme::SCREEN_H));
            page->setFillColor(theme::none());
            add(page);
            backdrop = new ui::Backdrop(FloatRect(560, 0, theme::SCREEN_W - 560, 780), 560, 320);
            page->add(backdrop);
            buildHero();
            if (!playlist) {
                rails.push_back({RailKind::Continue});
            }
            rails.push_back({RailKind::Recent});
            rails.push_back({RailKind::Favorites});
            for (size_t i = 0; i < rails.size(); i++) {
                buildRail(rails[i], RAILS_TOP + (float) i * RAIL_PITCH);
            }

            // ---- fixed chrome: fades so the page slides under the navigation and the hints
            auto *bottomFade = new GradientRectangle(FloatRect(0, theme::SCREEN_H - 160, theme::SCREEN_W, 160));
            bottomFade->setColor(theme::withAlpha(theme::background(), 0), theme::background(), GradientRectangle::Down);
            add(bottomFade);
            auto *topSolid = ui::box(this, FloatRect(0, 0, theme::SCREEN_W, 120), theme::background(), 0);
            (void) topSolid;
            topFade = new GradientRectangle(FloatRect(0, 120, theme::SCREEN_W, 70));
            topFade->setColor(theme::background(), theme::withAlpha(theme::background(), 0), GradientRectangle::Down);
            add(topFade);
            topSolid->setVisibility(Visibility::Hidden);   // shown once the page has scrolled (see layoutPage)
            this->topSolid = topSolid;

            std::vector<std::string> sections = {tr("nav.home"), tr("home.live_tv")};
            navIds = {NAV_HOME, NAV_LIVE};
            if (!playlist) {
                sections.push_back(tr("home.movies"));
                sections.push_back(tr("home.series"));
                navIds.push_back(NAV_MOVIES);
                navIds.push_back(NAV_SERIES);
            }
            for (int id: {NAV_SEARCH, NAV_FAVORITES, NAV_DOWNLOADS, NAV_SETTINGS}) {
                navIds.push_back(id);
            }
            nav = new ui::TopNav(tr("app.name"), sections, {ui::Icon::Search, ui::Icon::Favorites, ui::Icon::Downloads,
                                                             ui::Icon::Settings});
            nav->setCurrent(0);
            nav->setClock(clockx::localTime());
            add(nav);

            // under the brand: the source and its state, then an account notice (the right side is kept free for
            // the focused icon's name)
            auto *dot = new CircleShape(6);
            dot->setPointCount(14);
            dot->setFillColor(s.connected ? theme::success() : s.offline ? theme::warning() : theme::danger());
            dot->setPosition(theme::SAFE_X + 2, 140);
            add(dot);
            auto *who = ui::label(this, s.profile.name, theme::CAPTION, theme::SAFE_X + 22, 132, ui::Weight::SemiBold,
                                  theme::textSecondary());
            who->setMaxWidth(420);
            std::string notice;
            int64_t now = clockx::unixNow();
            if (s.offline) {
                notice = tr("home.offline_notice");
            } else if (s.httpsWarning) {
                notice = tr("home.https_notice");
            } else if (s.account.expiresAt > 0 && s.account.expiresAt - now < 7 * 86400) {
                notice = tr("home.expiry_notice", {clockx::localDate(s.account.expiresAt)});
            }
            if (!notice.empty()) {
                float nx = theme::SAFE_X + 22 + who->width() + theme::SPACE_L;
                auto *n = ui::label(this, notice, theme::CAPTION, nx, 132, ui::Weight::SemiBold, theme::warning());
                n->setMaxWidth(theme::SCREEN_W - theme::SAFE_X - nx - 260);
            }

            hints = screens::hintBar(this, {});
            rebuildRows();
            zone = HERO;
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

        bool animating() const override {
            return std::fabs(scrollPos - scrollTarget) > 0.5f;
        }

        void tick(double t) override {
            std::string time = clockx::localTime();
            if (time != nav->clockText()) {
                nav->setClock(time);
                redraw();
            }
            if (app.downloads().generation() != downloadsGen && t - lastDownloadsRefresh > 1.0) {
                lastDownloadsRefresh = t;   // the downloads badge (at most once per second)
                downloadsGen = app.downloads().generation();
                nav->setBadge(2, app.downloads().totals().active);
                redraw();
            }
            if (app.vod().generation() != vodGen || app.library().generation() != libraryGen
                || app.session().liveGeneration != liveGen) {
                rebuildRows();
                refresh();
                requestImages();
                redraw();
            } else if (app.images().generation() != imageGen) {
                bindImages();
                redraw();
            }
            // the page glides to its target (Smooth scrolling), at most 1/60 s steps
            if (animating()) {
                double dt = lastTick > 0 ? std::min(0.05, t - lastTick) : 1.0 / 60;
                float k = (float) std::min(1.0, dt * 14.0);
                scrollPos += (scrollTarget - scrollPos) * k;
                if (std::fabs(scrollPos - scrollTarget) <= 0.5f) {
                    scrollPos = scrollTarget;
                }
                page->setPosition(0, -std::round(scrollPos));
                redraw();
            }
            lastTick = t;
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Left:
                case PadButton::L1:
                    moveFocus(-1);
                    break;
                case PadButton::Right:
                case PadButton::R1:
                    moveFocus(1);
                    break;
                case PadButton::Down:
                    for (int z = zone + 1; z < RAIL + (int) rails.size(); z++) {
                        if (zoneSize(z) > 0) {
                            zone = z;
                            break;
                        }
                    }
                    break;
                case PadButton::Up:
                    for (int z = zone - 1; z >= NAV; z--) {
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
                        openNav(NAV_SEARCH);
                    }
                    return;
                case PadButton::Square:
                    if (!e.repeat && zone >= RAIL && rails[(size_t) (zone - RAIL)].kind == RailKind::Continue) {
                        Rail &r = rails[(size_t) (zone - RAIL)];
                        if (r.index < (int) r.items.size()) {
                            removeFromContinueWatching(r.items[(size_t) r.index].history);
                        }
                    }
                    return;
                case PadButton::Options:
                    if (!e.repeat) {
                        openNav(NAV_SETTINGS);
                    }
                    return;
                case PadButton::Circle:
                    if (e.repeat) {
                        return;
                    }
                    if (zone != HERO) {
                        zone = HERO;
                        break;
                    }
                    app.push(screens::makeDialog(app, tr("app.exit_title"), "", {tr("common.cancel"), tr("common.exit")},
                                                 [this](int c) {
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
        // ------------------------------------------------------------------ building
        void buildHero() {
            eyebrow = ui::label(page, "", theme::LABEL, theme::SAFE_X, HERO_TOP, ui::Weight::SemiBold, theme::accentText());
            heroTitle = ui::label(page, "", theme::DISPLAY, theme::SAFE_X, HERO_TOP + 40, ui::Weight::SemiBold,
                                  theme::textStrong());
            heroTitle->setMaxWidth(HERO_TEXT_W);
            heroTitle->setMaxLines(2);
            heroSubtitle = ui::label(page, "", theme::BODY, theme::SAFE_X, 0, ui::Weight::Regular, theme::textPrimary());
            heroSubtitle->setMaxWidth(HERO_TEXT_W);
            heroSubtitle->setMaxLines(2);
            heroMeta = ui::label(page, "", theme::LABEL, theme::SAFE_X, 0, ui::Weight::Regular, theme::textSecondary());
            heroMeta->setMaxWidth(HERO_TEXT_W);
            heroBar = new ui::ProgressBar(FloatRect(theme::SAFE_X, HERO_BUTTONS_Y - 44, 520, theme::PROGRESS_H));
            page->add(heroBar);
            heroTime = ui::label(page, "", theme::CAPTION, theme::SAFE_X + 540, HERO_BUTTONS_Y - 56, ui::Weight::SemiBold,
                                 theme::textSecondary());
            for (int i = 0; i < HERO_BUTTONS; i++) {
                heroButtons[i] = new ui::Button("", FloatRect(0, HERO_BUTTONS_Y, 300, theme::BUTTON_H), i == 0);
                page->add(heroButtons[i]);
            }
            heroPoster = new ui::PosterView(FloatRect(theme::SCREEN_W - theme::SAFE_X - HERO_POSTER_W, HERO_TOP - 10,
                                                      HERO_POSTER_W, HERO_POSTER_H), theme::HEADING);
            page->add(heroPoster);
        }

        void buildRail(Rail &r, float y) {
            r.y = y;
            const char *key = r.kind == RailKind::Continue ? "home.continue_watching"
                              : r.kind == RailKind::Recent ? "home.recently_watched" : "home.favorites";
            r.heading = ui::label(page, tr(key), theme::HEADING - 2, theme::SAFE_X, y, ui::Weight::SemiBold);
            r.detail = ui::label(page, "", theme::LABEL, theme::SAFE_X + r.heading->width() + theme::SPACE_L, y + 8,
                                 ui::Weight::Regular, theme::textSecondary());
            r.detail->setMaxWidth(theme::CONTENT_W - r.heading->width() - theme::SPACE_L);
            r.layer = new RectangleShape(FloatRect(theme::SAFE_X, y + RAIL_HEADING_H, theme::CONTENT_W, CARD_H));
            r.layer->setFillColor(theme::none());
            page->add(r.layer);
            r.empty = ui::label(page, "", theme::BODY, theme::SAFE_X, y + RAIL_HEADING_H + 50, ui::Weight::Regular,
                                theme::textMuted());
            r.empty->setMaxWidth(theme::CONTENT_W);
        }

        // ------------------------------------------------------------------ data
        int zoneSize(int z) const {
            if (z == NAV) {
                return nav->itemCount();
            }
            if (z == HERO) {
                return heroCount;
            }
            return (int) rails[(size_t) (z - RAIL)].items.size();
        }

        static std::string titleOf(const HistoryEntry &h) {
            return h.type == ContentType::Series && !h.seriesName.empty() ? h.seriesName : h.name;
        }

        // "S01E03 · Title" for an episode
        static std::string episodeLine(const HistoryEntry &h) {
            std::string code = fmt::episodeCode(h.season, h.episode);
            return code + " \xC2\xB7 " + (h.name.empty() ? tr("episode.number", {std::to_string(h.episode)}) : h.name);
        }

        // one line under a history card: episode code or the type
        static std::string kindLine(const HistoryEntry &h) {
            if (h.type == ContentType::Series) {
                return episodeLine(h);
            }
            return tr(h.type == ContentType::Movie ? "home.kind_movie" : "home.kind_live");
        }

        static std::string timeLine(const HistoryEntry &h) {
            return h.duration > 0 ? fmt::clock(h.position) + " / " + fmt::clock(h.duration) : fmt::clock(h.position);
        }

        void rebuildRows() {
            vodGen = app.vod().generation();
            libraryGen = app.library().generation();
            liveGen = app.session().liveGeneration;
            imageGen = app.images().generation();
            for (Rail &r: rails) {
                r.items.clear();
                switch (r.kind) {
                    case RailKind::Continue:
                        for (const HistoryEntry *h: app.library().continueWatching(CW_MAX)) {
                            RailItem it;
                            it.history = *h;
                            r.items.push_back(it);
                        }
                        break;
                    case RailKind::Recent:
                        for (const HistoryEntry *h: app.library().recentlyWatched(RECENT_MAX, RECENT_LIVE_MAX)) {
                            RailItem it;
                            it.history = *h;
                            r.items.push_back(it);
                        }
                        break;
                    case RailKind::Favorites:
                        collectFavorites(r.items);
                        break;
                }
                buildCards(r);
                r.index = std::min(r.index, std::max(0, (int) r.items.size() - 1));
                r.first = std::min(r.first, std::max(0, (int) r.items.size() - CARDS_VISIBLE));
            }
            hero = nullptr;
            if (!playlist && !rails.empty() && rails[0].kind == RailKind::Continue && !rails[0].items.empty()) {
                heroEntry = rails[0].items[0].history;
                hero = &heroEntry;
            }
            bindHero();
            if (zoneSize(zone) == 0) {
                zone = HERO;
            }
        }

        void collectFavorites(std::vector<RailItem> &out) {
            const Session &s = app.session();
            size_t total = 0;
            for (ContentType t: {ContentType::Live, ContentType::Movie, ContentType::Series}) {
                total += app.library().favorites(t).size();
            }
            if (total == 0) {
                return;
            }
            auto add = [&out](ItemKind kind, const std::vector<int> &indices) {
                for (int i: indices) {
                    if ((int) out.size() >= FAVORITES_MAX) {
                        return;
                    }
                    RailItem it;
                    it.kind = kind;
                    it.index = i;
                    out.push_back(it);
                }
            };
            if (s.liveLoaded) {
                add(ItemKind::Channel, s.live.favorites(app.library().favorites(ContentType::Live)));
            }
            if (!playlist) {
                // only catalogs already in memory: Home never starts a catalog load
                add(ItemKind::Movie, app.vod().movies().favorites(app.library().favorites(ContentType::Movie)));
                add(ItemKind::Series, app.vod().series().favorites(app.library().favorites(ContentType::Series)));
            }
            RailItem all;
            all.kind = ItemKind::SeeAll;
            out.push_back(all);
        }

        // artwork, title and two lines of one rail item
        void describe(const RailItem &it, std::string &title, std::string &second, std::string &third,
                      std::string &image, bool &live, float &progress) const {
            live = false;
            progress = 0;
            switch (it.kind) {
                case ItemKind::History: {
                    const HistoryEntry &h = it.history;
                    title = titleOf(h);
                    second = kindLine(h);
                    image = h.icon;
                    live = h.type == ContentType::Live;
                    if (!live && progress::inProgress(h.position, h.duration, h.watched)) {
                        third = fmt::remaining(h.position, h.duration);
                        progress = std::max(0.02f, (float) progress::fraction(h.position, h.duration));
                    } else if (h.watched) {
                        third = "\xE2\x9C\x93 " + tr("progress.watched");
                    }
                    return;
                }
                case ItemKind::Channel: {
                    const LiveChannel &c = app.session().live.channels()[(size_t) it.index];
                    title = c.name;
                    second = tr("home.kind_live");
                    third = c.num > 0 ? std::to_string(c.num) : "";
                    image = c.icon;
                    live = true;
                    return;
                }
                case ItemKind::Movie: {
                    const Movie &m = app.vod().movies().items()[(size_t) it.index];
                    title = m.title;
                    second = tr("home.kind_movie");
                    third = m.year > 0 ? std::to_string(m.year) : "";
                    image = m.icon;
                    return;
                }
                case ItemKind::Series: {
                    const Series &s = app.vod().series().items()[(size_t) it.index];
                    title = s.title;
                    second = tr("home.kind_series");
                    third = s.year > 0 ? std::to_string(s.year) : "";
                    image = s.cover;
                    return;
                }
                case ItemKind::SeeAll:
                    title = tr("home.see_all");
                    second = tr("home.see_all_detail");
                    return;
            }
        }

        void buildCards(Rail &r) {
            for (auto *c: r.layer->getChilds()) {
                r.layer->remove(c);
                delete c;
            }
            r.cards.clear();
            for (size_t i = 0; i < r.items.size(); i++) {
                const RailItem &it = r.items[i];
                std::string title, second, third, image;
                bool live = false;
                float progress = 0;
                describe(it, title, second, third, image, live, progress);
                Card c;
                c.image = image;
                c.live = live;
                c.bg = ui::box(r.layer, FloatRect((float) i * (CARD_W + CARD_GAP), 0, CARD_W, CARD_H), theme::card(),
                               theme::RADIUS);
                float tx;
                if (it.kind == ItemKind::SeeAll) {
                    tx = 32;
                    c.title = ui::label(c.bg, title, theme::BODY, tx, 42, ui::Weight::SemiBold, theme::accentText());
                    auto *k = ui::label(c.bg, second, theme::LABEL, tx, 84, ui::Weight::Regular, theme::textSecondary());
                    k->setMaxWidth(CARD_W - 2 * tx);
                } else {
                    if (live) {
                        c.logo = new ui::LogoView(FloatRect(16, (CARD_H - 84) / 2, 136, 84), theme::LABEL, 6, 1.25f);
                        c.bg->add(c.logo);
                        tx = 172;
                    } else {
                        c.poster = new ui::PosterView(FloatRect(0, 0, 100, CARD_H), theme::CAPTION);
                        c.bg->add(c.poster);
                        tx = 124;
                    }
                    const float tw = CARD_W - tx - 20;
                    c.title = ui::label(c.bg, title, theme::LABEL, tx, 22, ui::Weight::SemiBold);
                    c.title->setMaxWidth(tw);
                    auto *k = ui::label(c.bg, second, theme::CAPTION, tx, 58, ui::Weight::Regular, theme::textSecondary());
                    k->setMaxWidth(tw);
                    auto *m = ui::label(c.bg, third, theme::CAPTION, tx, 88, ui::Weight::Regular, theme::textMuted());
                    m->setMaxWidth(tw);
                    if (progress > 0) {
                        c.bar = new ui::ProgressBar(FloatRect(tx, CARD_H - 26, tw, theme::PROGRESS_H));
                        c.bar->setFraction(progress);
                        c.bg->add(c.bar);
                    }
                }
                r.cards.push_back(c);
                bindCardImage(r.cards.back(), title);
            }
            const char *emptyKey = r.kind == RailKind::Continue ? "home.continue_empty"
                                   : r.kind == RailKind::Recent ? "home.recent_empty" : "home.favorites_empty";
            r.empty->setText(r.items.empty() ? tr(emptyKey) : "");
            // favorites exist but only movies / series whose section was not opened yet: say why they are missing
            if (r.kind == RailKind::Favorites && r.items.size() == 1) {
                r.empty->setText(tr("home.favorites_more"));
                r.empty->setPosition(theme::SAFE_X + CARD_W + CARD_GAP + 8, r.y + RAIL_HEADING_H + 58);
                r.empty->setMaxWidth(theme::CONTENT_W - CARD_W - CARD_GAP - 8);
            } else {
                r.empty->setPosition(theme::SAFE_X, r.y + RAIL_HEADING_H + 50);
                r.empty->setMaxWidth(theme::CONTENT_W);
            }
        }

        void bindCardImage(Card &c, const std::string &title) {
            if (c.logo) {
                std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Logo, c.image);
                c.logo->set(title, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
            } else if (c.poster) {
                std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Poster, c.image);
                c.poster->set(title, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
            }
        }

        // images arrived: only the artwork is bound again (no labels are rebuilt)
        void bindImages() {
            imageGen = app.images().generation();
            for (Rail &r: rails) {
                for (size_t i = 0; i < r.cards.size(); i++) {
                    std::string title, second, third, image;
                    bool live = false;
                    float progress = 0;
                    describe(r.items[i], title, second, third, image, live, progress);
                    bindCardImage(r.cards[i], title);
                }
            }
            bindHeroImages();
        }

        // ------------------------------------------------------------------ hero
        void bindHero() {
            heroBar->setVisibility(Visibility::Hidden);
            heroTime->setText("");
            heroPoster->setVisibility(hero ? Visibility::Visible : Visibility::Hidden);
            if (hero) {
                const HistoryEntry &h = *hero;
                eyebrow->setText(tr("home.continue_watching"));
                heroTitle->setText(titleOf(h));
                heroSubtitle->setText(h.type == ContentType::Series ? episodeLine(h) : "");
                std::string left = fmt::remaining(h.position, h.duration);
                heroMeta->setText(tr(h.type == ContentType::Series ? "home.kind_series" : "home.kind_movie")
                                  + (left.empty() ? "" : "   \xC2\xB7   " + left));
                heroBar->setFraction(std::max(0.02f, (float) progress::fraction(h.position, h.duration)));
                heroBar->setVisibility(Visibility::Visible);
                heroTime->setText(timeLine(h));
                heroIds = {HERO_RESUME, HERO_DETAILS};
            } else {
                eyebrow->setText(tr("app.name"));
                heroTitle->setText(tr("home.hero_start_title"));
                heroSubtitle->setText(tr(playlist ? "home.hero_start_text_playlist" : "home.hero_start_text"));
                heroMeta->setText(sectionCounts());
                if (playlist) {
                    heroIds = {HERO_LIVE, HERO_SEARCH};
                } else {
                    heroIds = {HERO_LIVE, HERO_MOVIES, HERO_SERIES};
                }
            }
            heroCount = (int) heroIds.size();
            // stack the text under the title; the buttons keep their line
            float y = HERO_TOP + 40 + heroTitle->height() + 14;
            heroSubtitle->setPosition(theme::SAFE_X, y);
            y += heroSubtitle->getText().empty() ? 0 : heroSubtitle->height() + 12;
            heroMeta->setPosition(theme::SAFE_X, y);
            float x = theme::SAFE_X;
            for (int i = 0; i < HERO_BUTTONS; i++) {
                bool shown = i < heroCount;
                heroButtons[i]->setVisibility(shown ? Visibility::Visible : Visibility::Hidden);
                if (shown) {
                    float w = i == 0 && hero ? 400.0f : 280.0f;
                    heroButtons[i]->setWidth(w);
                    heroButtons[i]->setText(heroButtonText(i));
                    heroButtons[i]->setPrimary(i == 0);
                    heroButtons[i]->setPosition(x, HERO_BUTTONS_Y);
                    x += w + theme::SPACE_M;
                }
            }
            bindHeroImages();
        }

        std::string heroButtonText(int i) const {
            if (hero) {
                return i == 0 ? "\xE2\x96\xB6  " + tr("home.hero_resume_at", {fmt::clock(hero->position)})
                              : tr("home.hero_details");
            }
            switch (heroIds[(size_t) i]) {
                case HERO_LIVE:
                    return tr("home.live_tv");
                case HERO_MOVIES:
                    return tr("home.movies");
                case HERO_SERIES:
                    return tr("home.series");
                default:
                    return tr("home.search");
            }
        }

        // "573 channels · 19,798 movies" for what is loaded (never a load just for the count)
        std::string sectionCounts() const {
            const Session &s = app.session();
            std::string out;
            auto addPart = [&out](const std::string &p) {
                if (!p.empty()) {
                    out += (out.empty() ? "" : "   \xC2\xB7   ") + p;
                }
            };
            if (s.liveLoaded) {
                addPart(i18n::count("home.channels", (long long) s.live.channels().size()));
            }
            if (!playlist && !app.vod().movies().empty()) {
                addPart(i18n::count("home.movie_count", (long long) app.vod().movies().size()));
            }
            if (!playlist && !app.vod().series().empty()) {
                addPart(i18n::count("home.series_count", (long long) app.vod().series().size()));
            }
            return out;
        }

        void bindHeroImages() {
            if (!hero) {
                backdrop->set(nullptr, Vector2i());
                return;
            }
            // the large poster when it is ready, else the rail's poster (usually cached already)
            std::shared_ptr<ImageSet> big = app.images().get(ImageKind::PosterLarge, hero->icon);
            std::shared_ptr<ImageSet> small = big ? nullptr : app.images().get(ImageKind::Poster, hero->icon);
            const std::shared_ptr<ImageSet> &img = big ? big : small;
            backdrop->set(img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
            heroPoster->set(titleOf(*hero), img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
        }

        void requestImages() {
            if (!app.settings().get().loadImages) {
                return;
            }
            std::vector<ImageRequest> req;
            if (hero) {
                req.push_back({ImageKind::PosterLarge, hero->icon});
            }
            for (const Rail &r: rails) {
                for (const Card &c: r.cards) {
                    if (!c.image.empty()) {
                        req.push_back({c.live ? ImageKind::Logo : ImageKind::Poster, c.image});
                    }
                }
            }
            app.images().want(req);
        }

        // ------------------------------------------------------------------ actions
        void moveFocus(int d) {
            int n = zoneSize(zone);
            if (zone == NAV) {
                navIndex = std::min(std::max(navIndex + d, 0), n - 1);
            } else if (zone == HERO) {
                heroIndex = std::min(std::max(heroIndex + d, 0), n - 1);
            } else {
                Rail &r = rails[(size_t) (zone - RAIL)];
                r.index = std::min(std::max(r.index + d, 0), std::max(0, n - 1));
            }
        }

        void activate() {
            if (zone == NAV) {
                openNav(navIds[(size_t) navIndex]);
            } else if (zone == HERO) {
                activateHero(heroIds[(size_t) heroIndex]);
            } else {
                Rail &r = rails[(size_t) (zone - RAIL)];
                if (r.index >= (int) r.items.size()) {
                    return;
                }
                const RailItem &it = r.items[(size_t) r.index];
                switch (it.kind) {
                    case ItemKind::History:
                        if (r.kind == RailKind::Continue) {
                            resume(it.history);
                        } else {
                            openRecent(it.history);
                        }
                        return;
                    case ItemKind::Channel: {
                        // zapping goes through the favorite channels, like in Favorites
                        std::vector<int> list = app.session().live.favorites(app.library().favorites(ContentType::Live));
                        int at = 0;
                        for (size_t i = 0; i < list.size(); i++) {
                            if (list[i] == it.index) {
                                at = (int) i;
                            }
                        }
                        app.push(screens::makeLivePlayer(app, list.empty() ? std::vector<int>{it.index} : list, at));
                        return;
                    }
                    case ItemKind::Movie:
                        app.push(screens::makeMovieDetail(app, app.vod().movies().items()[(size_t) it.index]));
                        return;
                    case ItemKind::Series:
                        app.push(screens::makeSeriesDetail(app, app.vod().series().items()[(size_t) it.index]));
                        return;
                    case ItemKind::SeeAll:
                        app.push(screens::makeFavorites(app));
                        return;
                }
            }
        }

        enum HeroAction {
            HERO_RESUME,
            HERO_DETAILS,
            HERO_LIVE,
            HERO_MOVIES,
            HERO_SERIES,
            HERO_SEARCH
        };

        void activateHero(int action) {
            switch (action) {
                case HERO_RESUME:
                    if (hero) {
                        resume(*hero);
                    }
                    return;
                case HERO_DETAILS:
                    if (hero) {
                        openRecent(*hero);
                    }
                    return;
                case HERO_LIVE:
                    openNav(NAV_LIVE);
                    return;
                case HERO_MOVIES:
                    openNav(NAV_MOVIES);
                    return;
                case HERO_SERIES:
                    openNav(NAV_SERIES);
                    return;
                default:
                    openNav(NAV_SEARCH);
                    return;
            }
        }

        void openNav(int item) {
            switch (item) {
                case NAV_HOME:
                    zone = HERO;
                    refresh();
                    return;
                case NAV_LIVE:
                    app.push(screens::makeLive(app));
                    return;
                case NAV_MOVIES:
                    app.push(screens::makeMovies(app));
                    return;
                case NAV_SERIES:
                    app.push(screens::makeSeries(app));
                    return;
                case NAV_SEARCH:
                    app.push(screens::makeSearch(app));
                    return;
                case NAV_FAVORITES:
                    app.push(screens::makeFavorites(app));
                    return;
                case NAV_DOWNLOADS:
                    app.push(screens::makeDownloads(app));
                    return;
                default:
                    app.push(screens::makeSettings(app));
                    return;
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
            const std::string &profileId = app.session().profile.id;
            std::vector<dl::Item> local = h.type == ContentType::Series
                                          ? offline::seriesEpisodes(app.downloads(), profileId, h.seriesId)
                                          : std::vector<dl::Item>();
            if (h.type == ContentType::Series && app.session().offline && !local.empty()) {
                // offline: the downloaded episodes are the queue (next episode works without the provider)
                queue.clear();
                for (const dl::Item &d: local) {
                    if (d.contentId == h.id) {
                        position = (int) queue.size();
                    }
                    queue.push_back(offline::itemFor(app.downloads(), d));
                }
                bool found = false;
                for (const VodItem &it: queue) {
                    found |= it.id == h.id;
                }
                if (!found) {
                    queue.insert(queue.begin(), itemOf(h));
                    position = 0;
                }
            } else if (h.type == ContentType::Series) {
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
            for (VodItem &it: queue) {
                offline::preferLocal(app.downloads(), it, profileId);   // a downloaded copy plays from the disk
            }
            app.push(screens::makeVodPlayer(app, queue, position, true));
        }

        // hides the card at once (the next card moves into its place); progress, history and downloads stay
        void removeFromContinueWatching(const HistoryEntry &h) {
            if (!app.library().dismissFromContinueWatching(h.type, h.id)) {
                return;
            }
            app.saveLibrary();
            rebuildRows();
            refresh();
            requestImages();
            redraw();
            app.toast(tr("home.cw_removed"));
        }

        void openRecent(const HistoryEntry &h) {
            switch (h.type) {
                case ContentType::Live: {
                    const auto &live = app.session().live;
                    for (int i = 0; i < (int) live.channels().size(); i++) {
                        if (live.channels()[(size_t) i].id == h.id) {
                            app.push(screens::makeLivePlayer(app, {i}, 0));
                            return;
                        }
                    }
                    app.toast(tr("live.channel_gone"), ToastKind::Error);
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

        // ------------------------------------------------------------------ focus and layout
        void refresh() {
            navIndex = std::min(navIndex, nav->itemCount() - 1);
            heroIndex = std::min(heroIndex, std::max(0, heroCount - 1));
            nav->setFocus(zone == NAV ? navIndex : -1);
            if (zone == NAV && navIndex >= nav->sectionCount()) {
                static const char *names[] = {"home.search", "home.favorites", "home.downloads", "home.settings"};
                nav->setIconCaption(tr(names[navIndex - nav->sectionCount()]));
            }
            for (int i = 0; i < heroCount; i++) {
                heroButtons[i]->setFocused(zone == HERO && i == heroIndex);
            }
            for (size_t ri = 0; ri < rails.size(); ri++) {
                Rail &r = rails[ri];
                bool active = zone == RAIL + (int) ri;
                // whole cards: the focused one is always among the four on screen
                if (r.index < r.first) {
                    r.first = r.index;
                } else if (r.index >= r.first + CARDS_VISIBLE) {
                    r.first = r.index - CARDS_VISIBLE + 1;
                }
                r.layer->setPosition(theme::SAFE_X - (float) r.first * (CARD_W + CARD_GAP), r.y + RAIL_HEADING_H);
                for (int i = 0; i < (int) r.cards.size(); i++) {
                    Card &c = r.cards[(size_t) i];
                    bool f = active && i == r.index;
                    bool onScreen = i >= r.first && i < r.first + CARDS_VISIBLE;
                    c.bg->setVisibility(onScreen ? Visibility::Visible : Visibility::Hidden);
                    c.bg->setFillColor(f ? theme::cardHover() : theme::card());
                    c.bg->setOutlineColor(theme::accent());
                    c.bg->setOutlineThickness(f ? theme::FOCUS_BORDER : 0);
                    c.title->setColor(r.items[(size_t) i].kind == ItemKind::SeeAll ? theme::accentText()
                                      : f ? theme::textStrong() : theme::textPrimary());
                }
                // the focused card in full next to the heading (card titles are truncated)
                std::string detail;
                if (active && r.index < (int) r.items.size() && r.items[(size_t) r.index].kind == ItemKind::History) {
                    const HistoryEntry &h = r.items[(size_t) r.index].history;
                    detail = titleOf(h);
                    if (h.type == ContentType::Series) {
                        detail += "   \xC2\xB7   " + episodeLine(h);
                    }
                    if (r.kind == RailKind::Continue) {
                        detail += "   \xC2\xB7   " + timeLine(h);
                    }
                }
                r.detail->setText(detail);
            }
            layoutPage();
            refreshHints();
        }

        // the page scrolls so that the focused rail is completely above the hint bar
        void layoutPage() {
            float target = 0;
            if (zone >= RAIL) {
                const Rail &r = rails[(size_t) (zone - RAIL)];
                target = std::max(0.0f, r.y + RAIL_HEADING_H + CARD_H - VIEW_BOTTOM);
            }
            scrollTarget = target;
            if (!ui::smoothScrolling()) {
                scrollPos = target;
            }
            page->setPosition(0, -std::round(scrollPos));
            // once the page moves, the navigation sits on a solid band (rails slide under it)
            bool scrolled = target > 0;
            topSolid->setVisibility(scrolled ? Visibility::Visible : Visibility::Hidden);
            topFade->setVisibility(scrolled ? Visibility::Visible : Visibility::Hidden);
        }

        void refreshHints() {
            screens::Hints h;
            bool cw = zone >= RAIL && rails[(size_t) (zone - RAIL)].kind == RailKind::Continue;
            bool resumeAction = cw || (zone == HERO && hero && heroIndex == 0);
            h.push_back({ui::Glyph::Cross, tr(resumeAction ? "common.resume" : "common.open")});
            if (cw) {
                h.push_back({ui::Glyph::Square, tr("home.cw_remove")});
            }
            h.push_back({ui::Glyph::Triangle, tr("home.search")});
            h.push_back({ui::Glyph::Options, tr("home.settings")});
            h.push_back({ui::Glyph::Circle, tr(zone == HERO ? "common.exit" : "common.back")});
            hints->setHints(h);
        }

        static const int HERO_BUTTONS = 3;

        bool playlist = false;
        RectangleShape *page;
        RectangleShape *topSolid = nullptr;
        GradientRectangle *topFade = nullptr;
        ui::TopNav *nav;
        std::vector<int> navIds;
        ui::Backdrop *backdrop;
        ui::Label *eyebrow = nullptr;
        ui::Label *heroTitle = nullptr;
        ui::Label *heroSubtitle = nullptr;
        ui::Label *heroMeta = nullptr;
        ui::Label *heroTime = nullptr;
        ui::ProgressBar *heroBar = nullptr;
        ui::PosterView *heroPoster = nullptr;
        ui::Button *heroButtons[HERO_BUTTONS] = {nullptr, nullptr, nullptr};
        std::vector<int> heroIds;
        int heroCount = 0;
        HistoryEntry heroEntry;
        const HistoryEntry *hero = nullptr;
        std::vector<Rail> rails;
        ui::HintBar *hints;
        int zone = HERO;
        int navIndex = 0;
        int heroIndex = 0;
        float scrollPos = 0;
        float scrollTarget = 0;
        double lastTick = -1;
        unsigned liveGen = 0;
        unsigned vodGen = 0;
        unsigned imageGen = 0;
        unsigned libraryGen = 0;
        unsigned downloadsGen = (unsigned) -1;
        double lastDownloadsRefresh = -10;
    };

    class SectionScreen : public Screen {
    public:
        SectionScreen(App &a, const std::string &title, const std::string &message) : Screen(a) {
            ui::background(this);
            screens::header(this, title);
            auto *m = ui::label(this, message, theme::BODY, 0, 480, ui::Weight::Regular, theme::textSecondary());
            m->setAlign(ui::Align::Center, theme::SCREEN_W);
            m->setMaxWidth(1200);
            m->setMaxLines(3);
            screens::hintBar(this, {{ui::Glyph::Circle, tr("common.back")}});
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
