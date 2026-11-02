// Home: top navigation, one cinematic hero for the most meaningful action, and rails of landscape cards.
//
//   navigation   [V7 IPTV]  Home  Live TV  Movies  Series ........ (search)(favorites)(downloads)(settings)  clock
//   hero         the most recent Continue Watching item: eyebrow, title, episode, progress, Resume / Details, all
//                over one full-bleed layer made from its poster (no separate poster tile). Without one: "What
//                would you like to watch?" with the sections as buttons.
//   rails        Continue Watching, Recently Watched, Favorites (data the app already has, nothing invented)
//
// Geometry: ui/layout.h (layout::home), docs/v7-ui-v2-layouts.md. Cards are 16:9: movies and episodes show the
// upper part of their large poster (the grid poster until it arrives), channels their large logo on a tile.
//
// Continue Watching comes from the progress store (LibraryStore::continueWatching): movies and episodes in
// progress, most recent playback first, one card per series. It is rebuilt whenever the store changes
// (generation), so returning from the player shows the new position at once. Live TV never appears there;
// Recently Watched shows at most RECENT_LIVE_MAX channels while movies/episodes can fill the row.
// Square on a Continue Watching card removes it from the row only (LibraryStore::dismissFromContinueWatching):
// the resume position, history, favorite and any download stay; new playback progress brings it back.
//
// Favorites shows channels (the live list is loaded at sign-in), then movies and series whose catalog is already
// in memory (catalogs load lazily when their section opens; Home never loads them). When none of them can be
// shown the rail is one quiet line and takes no focus; the navigation's Favorites button opens the full list.
//
// Playlist (M3U) sources are Live TV only: no Movies / Series sections and no Continue Watching rail (playlist
// channels never have a resume position).
//
// The page scrolls vertically when a lower rail takes the focus (it glides when Smooth scrolling is on); rails
// show four cards and move by whole cards. At rest the first rail peeks out under the bottom fade.

#include "common.h"
#include "../app/offline.h"
#include "../core/format.h"
#include "../platform/clock.h"

using namespace c2d;
using namespace iptv;
using screens::VodItem;

namespace {

    namespace L = layout::home;

    const int CW_MAX = 10;
    const int RECENT_MAX = 10;
    const int RECENT_LIVE_MAX = 4;
    const int FAVORITES_MAX = 12;
    const float CARD_ANCHOR = 0.22f;       // which part of a portrait poster a 16:9 card keeps (faces, not credits)
    const float HERO_ANCHOR = 0.30f;

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
        Series        // favorite series
    };

    struct RailItem {
        ItemKind kind = ItemKind::History;
        HistoryEntry history;
        int index = -1;
    };

    // one card of a rail: 16:9 artwork (progress inside it), then a title and one secondary line under it
    struct Card {
        RectangleShape *root = nullptr;   // at the card's slot in the rail; not scaled
        ui::PosterView *poster = nullptr; // movies / episodes
        ui::LogoView *logo = nullptr;     // channels
        ui::Label *title = nullptr;
        ui::Label *meta = nullptr;
        std::string image;        // what the artwork shows (for rebinding when images arrive)
        bool live = false;
        bool focused = false;
    };

    struct Rail {
        RailKind kind;
        RectangleShape *layer = nullptr;   // the cards, moved horizontally by whole cards
        ui::Label *heading = nullptr;
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
            backdrop = new ui::Backdrop(FloatRect(L::BACKDROP_X, 0, theme::SCREEN_W - L::BACKDROP_X, L::BACKDROP_H),
                                        L::BACKDROP_FADE_LEFT, L::BACKDROP_FADE_BOTTOM, L::BACKDROP_FADE_TOP);
            backdrop->setAnchor(HERO_ANCHOR);
            page->add(backdrop);
            buildHero();
            if (!playlist) {
                rails.push_back({RailKind::Continue});
            }
            rails.push_back({RailKind::Recent});
            rails.push_back({RailKind::Favorites});
            for (size_t i = 0; i < rails.size(); i++) {
                buildRail(rails[i], L::RAILS_Y + (float) i * L::RAIL_PITCH);
            }

            // ---- fixed chrome: the page slides under a band behind the navigation and the hints
            screens::bottomFade(this);
            topSolid = ui::box(this, FloatRect(0, 0, theme::SCREEN_W, L::NAV_BAND_H), theme::background(), 0);
            topFade = ui::fade(this, FloatRect(0, L::NAV_BAND_H, theme::SCREEN_W, L::NAV_FADE_H), theme::background(),
                               ui::Edge::Top);
            topSolid->setVisibility(Visibility::Hidden);   // shown once the page has scrolled (see layoutPage)
            topFade->setVisibility(Visibility::Hidden);

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

            // under the brand, only when there is something to know: offline, plain HTTP, account expiring
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
                auto *n = ui::label(this, notice, theme::CAPTION, theme::SAFE_X, L::NAV_Y + L::NAV_H + 6,
                                    ui::Weight::SemiBold, theme::warning());
                n->setMaxWidth(900);
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
            eyebrow = ui::label(page, "", theme::LABEL, L::TEXT_X, 0, ui::Weight::SemiBold, theme::accentText());
            eyebrow->setMaxWidth(L::TEXT_W);
            heroTitle = ui::label(page, "", theme::DISPLAY, L::TEXT_X, 0, ui::Weight::SemiBold, theme::textStrong());
            heroTitle->setMaxWidth(L::TEXT_W);
            heroTitle->setMaxLines(2);
            heroSubtitle = ui::label(page, "", theme::BODY, L::TEXT_X, 0, ui::Weight::Regular, theme::textPrimary());
            heroSubtitle->setMaxWidth(L::TEXT_W);
            heroSubtitle->setMaxLines(2);
            heroMeta = ui::label(page, "", theme::LABEL, L::TEXT_X, 0, ui::Weight::Regular, theme::textSecondary());
            heroMeta->setMaxWidth(L::TEXT_W);
            const float barY = L::BUTTONS_Y - L::PROGRESS_TO_BUTTONS;
            heroBar = new ui::ProgressBar(FloatRect(L::TEXT_X, barY, L::PROGRESS_W, theme::PROGRESS_H));
            page->add(heroBar);
            heroTime = ui::label(page, "", theme::CAPTION, L::TEXT_X + L::PROGRESS_W + 20,
                                 barY + theme::PROGRESS_H / 2 - std::round(theme::CAPTION * 1.25f / 2),
                                 ui::Weight::SemiBold, theme::textSecondary());
            for (int i = 0; i < HERO_BUTTONS; i++) {
                heroButtons[i] = new ui::Button("", FloatRect(0, L::BUTTONS_Y, L::SECONDARY_W, L::BUTTON_H), i == 0);
                page->add(heroButtons[i]);
            }
        }

        void buildRail(Rail &r, float y) {
            r.y = y;
            const char *key = r.kind == RailKind::Continue ? "home.continue_watching"
                              : r.kind == RailKind::Recent ? "home.recently_watched" : "home.favorites";
            r.heading = ui::label(page, tr(key), theme::HEADING - 2, theme::SAFE_X, y, ui::Weight::SemiBold);
            r.layer = new RectangleShape(FloatRect(theme::SAFE_X, y + L::RAIL_HEADING_H, theme::CONTENT_W, L::CARD_H));
            r.layer->setFillColor(theme::none());
            page->add(r.layer);
            r.empty = ui::label(page, "", theme::BODY, theme::SAFE_X, y + L::RAIL_HEADING_H + 8, ui::Weight::Regular,
                                theme::textMuted());
            r.empty->setMaxWidth(theme::CONTENT_W);
            r.empty->setMaxLines(2);
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
                r.first = std::min(r.first, std::max(0, (int) r.items.size() - L::CARDS_VISIBLE));
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
            favoritesTotal = total;
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
                c.root = new RectangleShape(FloatRect((float) i * (L::CARD_W + L::CARD_GAP), 0, L::CARD_W, L::CARD_H));
                c.root->setFillColor(theme::none());
                r.layer->add(c.root);
                const FloatRect art(0, 0, L::CARD_W, L::CARD_ART_H);
                if (live) {
                    // the large logo variant, centred on a tile (dark logos stay readable)
                    c.logo = new ui::LogoView(art, theme::DISPLAY, 44, 1.0f);
                    c.logo->setCornersRadius(theme::RADIUS);
                    c.root->add(c.logo);
                } else {
                    c.poster = new ui::PosterView(art, theme::LABEL);
                    c.poster->setCornersRadius(theme::RADIUS);
                    c.poster->setCover(true, CARD_ANCHOR);
                    c.poster->setFocusScale(L::CARD_FOCUS_SCALE);
                    c.poster->setProgress(progress);
                    c.root->add(c.poster);
                }
                const float ty = L::CARD_ART_H + L::CARD_TITLE_GAP;
                c.title = ui::label(c.root, title, theme::LABEL, 0, ty, ui::Weight::SemiBold, theme::textPrimary());
                c.title->setMaxWidth(L::CARD_W);
                std::string meta = second;
                if (!third.empty()) {
                    meta += (meta.empty() ? "" : "  \xC2\xB7  ") + third;
                }
                c.meta = ui::label(c.root, meta, theme::CAPTION, 0, ty + 34, ui::Weight::Regular, theme::textSecondary());
                c.meta->setMaxWidth(L::CARD_W);
                r.cards.push_back(c);
                bindCardImage(r.cards.back(), title);
            }
            const char *emptyKey = r.kind == RailKind::Continue ? "home.continue_empty"
                                   : r.kind == RailKind::Recent ? "home.recent_empty" : "home.favorites_empty";
            // favorites exist but only movies / series whose section was not opened yet: say why they are missing
            if (r.kind == RailKind::Favorites && r.items.empty() && favoritesTotal > 0) {
                emptyKey = "home.favorites_more";
            }
            r.empty->setText(r.items.empty() ? tr(emptyKey) : "");
        }

        // movies / episodes: the large poster (sharp at card size), the grid poster until it is ready
        void bindCardImage(Card &c, const std::string &title) {
            if (c.logo) {
                std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Logo, c.image);
                bool large = img && img->variants.size() > 1;
                c.logo->set(title, img ? img->at(large ? 1 : 0).texture : nullptr,
                            img ? img->at(large ? 1 : 0).size : Vector2i());
            } else if (c.poster) {
                std::shared_ptr<ImageSet> img = app.images().get(ImageKind::PosterLarge, c.image);
                if (!img) {
                    img = app.images().get(ImageKind::Poster, c.image);
                }
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
            // the text stack hangs above the buttons (they keep their line): meta, episode, title, eyebrow
            float y = hero ? L::BUTTONS_Y - L::PROGRESS_TO_BUTTONS - 22 : L::BUTTONS_Y - 40;
            if (!heroMeta->getText().empty()) {
                y -= heroMeta->height();
                heroMeta->setPosition(L::TEXT_X, y);
                y -= 6;
            }
            if (!heroSubtitle->getText().empty()) {
                y -= heroSubtitle->height();
                heroSubtitle->setPosition(L::TEXT_X, y);
                y -= 8;
            }
            y -= heroTitle->height();
            heroTitle->setPosition(L::TEXT_X, y);
            y -= 6 + eyebrow->height();
            eyebrow->setPosition(L::TEXT_X, y);
            float x = L::TEXT_X;
            for (int i = 0; i < HERO_BUTTONS; i++) {
                bool shown = i < heroCount;
                heroButtons[i]->setVisibility(shown ? Visibility::Visible : Visibility::Hidden);
                if (shown) {
                    float w = i == 0 && hero ? L::RESUME_W : L::SECONDARY_W;
                    heroButtons[i]->setWidth(w);
                    heroButtons[i]->setText(heroButtonText(i));
                    heroButtons[i]->setPrimary(i == 0);
                    heroButtons[i]->setPosition(x, L::BUTTONS_Y);
                    x += w + L::BUTTON_GAP;
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

        // the hero layer reuses the large poster the first Continue Watching card shows (one texture, one request)
        void bindHeroImages() {
            if (!hero) {
                backdrop->set(nullptr, Vector2i());
                return;
            }
            std::shared_ptr<ImageSet> big = app.images().get(ImageKind::PosterLarge, hero->icon);
            std::shared_ptr<ImageSet> small = big ? nullptr : app.images().get(ImageKind::Poster, hero->icon);
            const std::shared_ptr<ImageSet> &img = big ? big : small;
            backdrop->set(img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
        }

        // the hero's poster, then the cards on screen in every rail (+ the next one): large posters / logos
        void requestImages() {
            if (!app.settings().get().loadImages) {
                return;
            }
            std::vector<ImageRequest> req;
            if (hero) {
                req.push_back({ImageKind::PosterLarge, hero->icon});
            }
            for (const Rail &r: rails) {
                int last = std::min((int) r.cards.size(), r.first + L::CARDS_VISIBLE + 1);
                for (int i = r.first; i < last; i++) {
                    const Card &c = r.cards[(size_t) i];
                    if (!c.image.empty()) {
                        req.push_back({c.live ? ImageKind::Logo : ImageKind::PosterLarge, c.image});
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
                int before = r.first;
                if (r.index < r.first) {
                    r.first = r.index;
                } else if (r.index >= r.first + L::CARDS_VISIBLE) {
                    r.first = r.index - L::CARDS_VISIBLE + 1;
                }
                if (r.first != before) {
                    imagesDue = true;   // other cards came on screen
                }
                r.layer->setPosition(theme::SAFE_X - (float) r.first * (L::CARD_W + L::CARD_GAP), r.y + L::RAIL_HEADING_H);
                for (int i = 0; i < (int) r.cards.size(); i++) {
                    Card &c = r.cards[(size_t) i];
                    bool f = active && i == r.index;
                    bool onScreen = i >= r.first && i < r.first + L::CARDS_VISIBLE;
                    c.root->setVisibility(onScreen ? Visibility::Visible : Visibility::Hidden);
                    focusCard(c, f);
                }
            }
            layoutPage();
            refreshHints();
            if (imagesDue) {
                imagesDue = false;
                requestImages();
            }
        }

        // focused: the artwork grows a little, gets the accent outline and full brightness; the title brightens
        static void focusCard(Card &c, bool f) {
            if (c.poster) {
                c.poster->setFocused(f);
                c.poster->setDimmed(!f);
            } else if (c.logo && f != c.focused) {
                const float s = L::CARD_FOCUS_SCALE;
                const float dx = L::CARD_W * (s - 1) / 2;
                const float dy = L::CARD_ART_H * (s - 1) / 2;
                c.logo->setScale(f ? s : 1.0f, f ? s : 1.0f);
                c.logo->setPosition(f ? -dx : 0, f ? -dy : 0);
            }
            if (c.logo) {
                c.logo->setOutlineColor(theme::accent());
                c.logo->setOutlineThickness(f ? theme::FOCUS_BORDER : 0);
            }
            c.focused = f;
            c.title->setColor(f ? theme::textStrong() : theme::textPrimary());
            c.meta->setColor(f ? theme::textPrimary() : theme::textSecondary());
        }

        // the page scrolls so that the focused rail is completely above the hint bar
        void layoutPage() {
            float target = 0;
            if (zone >= RAIL) {
                const Rail &r = rails[(size_t) (zone - RAIL)];
                target = std::max(0.0f, r.y + L::RAIL_HEADING_H + L::CARD_H - L::VIEW_BOTTOM);
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
        RectangleShape *topFade = nullptr;
        ui::TopNav *nav;
        std::vector<int> navIds;
        ui::Backdrop *backdrop;
        ui::Label *eyebrow = nullptr;
        ui::Label *heroTitle = nullptr;
        ui::Label *heroSubtitle = nullptr;
        ui::Label *heroMeta = nullptr;
        ui::Label *heroTime = nullptr;
        ui::ProgressBar *heroBar = nullptr;
        ui::Button *heroButtons[HERO_BUTTONS] = {nullptr, nullptr, nullptr};
        std::vector<int> heroIds;
        int heroCount = 0;
        HistoryEntry heroEntry;
        const HistoryEntry *hero = nullptr;
        std::vector<Rail> rails;
        size_t favoritesTotal = 0;
        bool imagesDue = false;
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
