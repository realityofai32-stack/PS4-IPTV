// Live TV: categories | channels | details.
//
// Lists are virtualized (ui::ListView: only the rows on screen exist and are re-bound when scrolling).
// Channel logos come from ImageLoader: the selected channel, the visible rows and - once scrolling pauses
// - a small window above/below are requested; rows show the initials until a logo is ready.
//
// Playlist (M3U) sources use the same browser: Favorites, All Channels, then the playlist's groups in playlist
// order (Uncategorized last). Options opens Refresh Playlist / Playlist Info. A refreshed playlist replaces the
// catalog (Session::liveGeneration): the screen rebuilds from channel / category ids, never from old indices.

#include <unordered_map>

#include "common.h"
#include "../platform/clock.h"
#include "../platform/log.h"

using namespace c2d;
using namespace iptv;

namespace {

    const int FAVORITES_ROW = 0;
    const int ALL_ROW = 1;
    const int FIRST_CATEGORY_ROW = 2;

    // layout (1920x1080)
    const float TOP = 200;
    const float LIST_H = 760;
    const float CAT_X = theme::SAFE_X;
    const float CAT_W = 400;
    const float CH_X = CAT_X + CAT_W + 36;
    const float CH_W = 820;
    const float PANE_X = CH_X + CH_W + 36;
    const float PANE_W = theme::SCREEN_W - theme::SAFE_X - PANE_X;
    const float CAT_ROW_H = 62;
    const float CH_ROW_H = 76;
    const float ROW_GAP = 8;

    // channel row: number | logo tile | name | star
    const float NUM_W = 64;
    const float LOGO_X = 80;
    const float LOGO_W = 104;
    const float LOGO_H = 60;
    const float NAME_X = LOGO_X + LOGO_W + 20;

    // details logo tile
    const float DETAIL_LOGO_W = 380;
    const float DETAIL_LOGO_H = 220;

    const int PREFETCH_ROWS = 8;         // above and below the visible rows, once scrolling pauses
    const double PREFETCH_PAUSE = 0.30;  // seconds without movement before prefetching

    class LiveScreen : public Screen {
    public:
        LiveScreen(App &a, bool startOnFavorites) : Screen(a), categoriesAdapter(this), channelsAdapter(this) {
            ui::background(this);
            playlist = app.session().profile.isPlaylist();
            screens::header(this, tr("home.live_tv"));
            notice = ui::label(this, "", theme::LABEL, theme::SAFE_X, theme::SAFE_Y + 62, ui::Weight::Regular,
                               theme::textSecondary());
            notice->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X);
            generation = app.session().liveGeneration;
            updateNotice();

            ui::label(this, tr("live.categories_caption"), theme::CAPTION, CAT_X + 20, TOP - 40, ui::Weight::SemiBold,
                      theme::textMuted());
            channelCaption = ui::label(this, "", theme::CAPTION, CH_X + 20, TOP - 40, ui::Weight::SemiBold,
                                       theme::textMuted());
            channelCaption->setMaxWidth(ui::ListView::rowWidth(CH_W) - 40);

            categoryList = new ui::ListView(FloatRect(CAT_X, TOP, CAT_W, LIST_H), CAT_ROW_H, 6, &categoriesAdapter);
            add(categoryList);
            channelList = new ui::ListView(FloatRect(CH_X, TOP, CH_W, LIST_H), CH_ROW_H, ROW_GAP, &channelsAdapter);
            add(channelList);
            empty = ui::label(this, "", theme::BODY, CH_X, TOP + 300, ui::Weight::Regular, theme::textMuted());
            empty->setAlign(ui::Align::Center, ui::ListView::rowWidth(CH_W));
            empty->setMaxWidth(ui::ListView::rowWidth(CH_W) - 80);
            empty->setMaxLines(3);

            buildDetails();
            hints = screens::hintBar(this, {});
            categoryList->setSelected(startOnFavorites ? FAVORITES_ROW : ALL_ROW);
            selectCategory(categoryList->selected());
            setFocus(startOnFavorites && !visible.empty() ? 1 : 0);
        }

        const char *name() const override { return "live"; }

        void onPause() override {
            app.images().want(std::vector<std::string>());   // no logo downloads competing with the stream
        }

        void onResume() override {
            if (app.session().liveGeneration != generation) {
                listReplaced();   // refreshed while another screen was on top
            }
            // favorites may have changed in the player or in search
            categoryList->reload();
            if (categoryList->selected() == FAVORITES_ROW) {
                selectCategory(FAVORITES_ROW, true);
            }
            // back from the player: focus the channel that was playing last (after zapping)
            if (!returnStreamId.empty()) {
                const auto &channels = app.session().live.channels();
                for (size_t i = 0; i < visible.size(); i++) {
                    if (channels[(size_t) visible[i]].id == returnStreamId) {
                        channelList->setSelected((int) i);
                        setFocus(1);
                        break;
                    }
                }
                returnStreamId.clear();
            }
            channelList->reload();
            refreshDetails();
            requestLogos(true);
        }

        void tick(double now) override {
            if (app.session().liveGeneration != generation) {
                listReplaced();
                redraw();
            }
            unsigned gen = app.images().generation();
            if (gen != logoGeneration) {
                logoGeneration = gen;
                channelList->reload();   // re-bind the visible rows: cheap, no objects are created
                refreshDetails();
                redraw();
            }
            if (prefetchPending && now - lastMoveAt >= PREFETCH_PAUSE) {
                prefetchPending = false;
                requestLogos(true);
            }
        }

        void handleInput(const InputEvent &e) override {
            ui::ListView *list = focus == 0 ? categoryList : channelList;
            int before = list->selected();
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    break;
                case PadButton::Down:
                    list->moveSelection(1);
                    break;
                case PadButton::L2:
                    list->moveSelection(-list->pageSize());
                    break;
                case PadButton::R2:
                    list->moveSelection(list->pageSize());
                    break;
                case PadButton::Left:
                    if (focus == 1) {
                        setFocus(0);
                    }
                    return;
                case PadButton::Right:
                    if (focus == 0 && !visible.empty()) {
                        setFocus(1);
                    }
                    return;
                case PadButton::L1:
                case PadButton::R1: {
                    int target = categoryList->selected() + (e.button == PadButton::L1 ? -1 : 1);
                    if (target >= 0 && target < categoriesAdapter.count()) {
                        categoryList->setSelected(target);
                        selectCategory(target);
                        if (focus == 1 && visible.empty()) {
                            setFocus(0);
                        }
                        moved(e.repeat);
                    }
                    return;
                }
                case PadButton::Cross:
                    if (e.repeat) {
                        return;
                    }
                    if (focus == 0) {
                        if (!visible.empty()) {
                            setFocus(1);
                        }
                    } else if (!visible.empty()) {
                        play(channelList->selected());
                    }
                    return;
                case PadButton::Square:
                    if (!e.repeat && focus == 1 && !visible.empty()) {
                        toggleFavorite();
                    }
                    return;
                case PadButton::Triangle:
                    if (!e.repeat) {
                        app.push(screens::makeSearch(app));
                    }
                    return;
                case PadButton::Options:
                    if (!e.repeat && playlist) {
                        playlistMenu();
                    }
                    return;
                case PadButton::Circle:
                    if (e.repeat) {
                        return;
                    }
                    if (focus == 1) {
                        setFocus(0);
                    } else {
                        app.pop();
                    }
                    return;
                default:
                    return;
            }
            if (list->selected() == before) {
                return;   // at the end of the list
            }
            if (focus == 0) {
                selectCategory(categoryList->selected());
            }
            refreshDetails();
            moved(e.repeat);
        }

    private:
        // ------------------------------------------------------------------ adapters
        struct CategoriesAdapter : ui::ListView::Adapter {
            explicit CategoriesAdapter(LiveScreen *s) : screen(s) {}

            int count() override {
                return FIRST_CATEGORY_ROW + (int) screen->app.session().live.categories().size();
            }

            C2DObject *createRow(float w, float h) override {
                Row r;
                r.bg = ui::box(nullptr, FloatRect(0, 0, w, h), theme::none(), theme::RADIUS_SMALL);
                r.marker = ui::box(r.bg, FloatRect(0, 16, 4, h - 32), theme::accent(), 2);
                r.name = ui::label(r.bg, "", theme::BODY, 22, ui::Label::centerOffset(theme::BODY, h));
                r.name->setMaxWidth(w - 22 - 84);
                r.count = ui::label(r.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h),
                                    ui::Weight::Regular, theme::textMuted());
                r.count->setAlign(ui::Align::Right, w - 20);
                rows.push_back(r);
                return r.bg;
            }

            void bindRow(C2DObject *obj, int index, bool selected, bool focused) override {
                const LiveCatalog &live = screen->app.session().live;
                for (auto &r: rows) {
                    if (r.bg != obj) {
                        continue;
                    }
                    std::string name;
                    int n;
                    if (index == FAVORITES_ROW) {
                        name = "\xE2\x98\x85  " + tr("home.favorites");
                        n = (int) live.favorites(screen->app.library().favorites(ContentType::Live)).size();
                    } else if (index == ALL_ROW) {
                        name = tr("live.all_channels");
                        n = (int) live.channels().size();
                    } else {
                        const Category &c = live.categories()[(size_t) (index - FIRST_CATEGORY_ROW)];
                        name = live.categoryName(c.id);   // Uncategorized in the current UI language
                        n = live.countInCategory(c.id);
                    }
                    r.name->setText(name);
                    r.count->setText(std::to_string(n));
                    r.bg->setFillColor(focused ? theme::focus() : selected ? theme::surface() : theme::none());
                    r.bg->setOutlineColor(theme::accent());
                    r.bg->setOutlineThickness(focused ? 3 : 0);
                    // the open category keeps an accent marker while the channel list has focus
                    r.marker->setVisibility(selected && !focused ? Visibility::Visible : Visibility::Hidden);
                    r.name->setWeight(selected ? ui::Weight::SemiBold : ui::Weight::Regular);
                    r.name->setColor(focused ? theme::textStrong() : selected ? theme::textPrimary() : theme::textSecondary());
                    r.count->setColor(focused ? theme::textPrimary() : theme::textMuted());
                }
            }

            struct Row {
                RectangleShape *bg;
                RectangleShape *marker;
                ui::Label *name;
                ui::Label *count;
            };
            LiveScreen *screen;
            std::vector<Row> rows;
        };

        struct ChannelsAdapter : ui::ListView::Adapter {
            explicit ChannelsAdapter(LiveScreen *s) : screen(s) {}

            int count() override { return (int) screen->visible.size(); }

            C2DObject *createRow(float w, float h) override {
                Row r;
                r.bg = ui::box(nullptr, FloatRect(0, 0, w, h), theme::surface(), theme::RADIUS_SMALL);
                r.num = ui::label(r.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h),
                                  ui::Weight::Regular, theme::textMuted());
                r.num->setAlign(ui::Align::Right, NUM_W);
                r.logo = new ui::LogoView(FloatRect(LOGO_X, (h - LOGO_H) / 2, LOGO_W, LOGO_H), 22, 4, 1.25f);
                r.bg->add(r.logo);
                r.name = ui::label(r.bg, "", theme::BODY, NAME_X, ui::Label::centerOffset(theme::BODY, h));
                r.name->setMaxWidth(w - NAME_X - 64);
                r.fav = ui::label(r.bg, "", theme::BODY, 0, ui::Label::centerOffset(theme::BODY, h),
                                  ui::Weight::SemiBold, theme::warning());
                r.fav->setAlign(ui::Align::Right, w - 22);
                rows.push_back(r);
                return r.bg;
            }

            void bindRow(C2DObject *obj, int index, bool selected, bool focused) override {
                const LiveCatalog &live = screen->app.session().live;
                const LiveChannel &c = live.channels()[(size_t) screen->visible[(size_t) index]];
                for (auto &r: rows) {
                    if (r.bg != obj) {
                        continue;
                    }
                    std::shared_ptr<ImageSet> img = screen->app.images().get(c.icon);
                    r.logo->set(c.name, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                    r.num->setText(c.num > 0 ? std::to_string(c.num) : "");
                    r.name->setText(c.name);
                    r.name->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                    r.name->setColor(focused ? theme::textStrong() : theme::textPrimary());
                    r.num->setColor(focused ? theme::textPrimary() : theme::textMuted());
                    r.fav->setText(screen->app.library().isFavorite(ContentType::Live, c.id) ? "\xE2\x98\x85" : "");
                    // selected while the categories have focus: show where the channel cursor is
                    r.bg->setFillColor(focused ? theme::focus() : selected ? theme::surfaceElevated()
                                                                              : theme::surface());
                    r.bg->setOutlineColor(theme::accent());
                    r.bg->setOutlineThickness(focused ? 3 : 0);
                }
            }

            struct Row {
                RectangleShape *bg;
                ui::Label *num;
                ui::LogoView *logo;
                ui::Label *name;
                ui::Label *fav;
            };
            LiveScreen *screen;
            std::vector<Row> rows;
        };

        // ------------------------------------------------------------------ details panel
        void buildDetails() {
            pane = ui::box(this, FloatRect(PANE_X, TOP, PANE_W, LIST_H), theme::surface(), theme::RADIUS);
            detailLogo = new ui::LogoView(FloatRect((PANE_W - DETAIL_LOGO_W) / 2, 28, DETAIL_LOGO_W, DETAIL_LOGO_H),
                                          72, 16, 1.6f);
            pane->add(detailLogo);
            detailName = ui::label(pane, "", theme::HEADING, 28, 0, ui::Weight::SemiBold);
            detailName->setMaxWidth(PANE_W - 56);
            detailName->setMaxLines(2);
            detailCategory = ui::label(pane, "", theme::LABEL, 28, 0, ui::Weight::Regular, theme::textSecondary());
            detailCategory->setMaxWidth(PANE_W - 56);
            detailCategory->setMaxLines(2);
            detailNumber = ui::label(pane, "", theme::LABEL, 28, 0, ui::Weight::Regular, theme::textMuted());
            detailFav = ui::label(pane, "", theme::LABEL, 28, 0, ui::Weight::SemiBold, theme::warning());

            const float bw = PANE_W - 56;
            watch = ui::box(pane, FloatRect(28, LIST_H - 28 - 72, bw, 72), theme::surfaceElevated(), 36);
            auto *wl = ui::label(watch, tr("live.watch"), theme::BODY, 0, ui::Label::centerOffset(theme::BODY, 72),
                                 ui::Weight::SemiBold);
            float textW = wl->width();
            const float glyph = 36;
            float groupX = (bw - (glyph + 14 + textW)) / 2;
            auto *g = new ui::ButtonGlyph(ui::Glyph::Cross, glyph);
            g->setPosition(std::round(groupX), 18);
            watch->add(g);
            wl->setPosition(std::round(groupX + glyph + 14), ui::Label::centerOffset(theme::BODY, 72));
        }

        void refreshDetails() {
            const LiveChannel *c = selectedChannel();
            focusedId = c ? c->id : std::string();
            bool show = c != nullptr;
            for (C2DObject *o: std::initializer_list<C2DObject *>{detailLogo, detailName, detailCategory, detailNumber,
                                                                  detailFav, watch}) {
                o->setVisibility(show ? Visibility::Visible : Visibility::Hidden);
            }
            if (!show) {
                return;
            }
            std::shared_ptr<ImageSet> img = app.images().get(c->icon);
            detailLogo->set(c->name, img ? img->at(1).texture : nullptr, img ? img->at(1).size : Vector2i());
            detailName->setText(c->name);
            float y = 28 + DETAIL_LOGO_H + 30;
            detailName->setPosition(28, y);
            y += detailName->height() + 12;
            detailCategory->setText(categoryName(c->categoryId));
            detailCategory->setPosition(28, y);
            if (!detailCategory->getText().empty()) {
                y += detailCategory->height() + 6;
            }
            detailNumber->setText(c->num > 0 ? tr("live.channel_number", {std::to_string(c->num)}) : "");
            detailNumber->setPosition(28, y);
            if (c->num > 0) {
                y += detailNumber->height() + 6;
            }
            detailFav->setText(app.library().isFavorite(ContentType::Live, c->id)
                               ? "\xE2\x98\x85  " + tr("favorites.in_favorites") : "");
            detailFav->setPosition(28, y + 8);
            watch->setFillColor(focus == 1 ? theme::accent() : theme::surfaceElevated());
        }

        // ------------------------------------------------------------------ behaviour
        std::string categoryName(const std::string &id) const {
            return app.session().live.categoryName(id);
        }

        // ------------------------------------------------------------------ playlist sources
        void updateNotice() {
            const Session &s = app.session();
            notice->setText(playlist && s.playlistRefreshing ? tr("m3u.refreshing") : s.liveNotice);
        }

        void playlistMenu() {
            app.push(screens::makeMenu(app, tr("m3u.options"), {tr("m3u.refresh"), tr("m3u.info")}, -1,
                                       whileAlive([this](int c) {
                                           if (c == 0) {
                                               app.refreshPlaylist(true);
                                               updateNotice();
                                           } else if (c == 1) {
                                               app.push(screens::makePlaylistInfo(app));
                                           }
                                       })));
        }

        // the catalog was replaced (playlist refresh): every index this screen holds is stale. Rebuild from the
        // open category's id and the focused channel's id.
        void listReplaced() {
            generation = app.session().liveGeneration;
            std::string categoryId = openCategoryId;
            std::string keep = focusedId;
            visible.clear();                 // never read through an old index again
            lastInCategory.clear();
            categoryList->reload();
            int row = currentCategoryRow < FIRST_CATEGORY_ROW ? std::max(currentCategoryRow, 0) : ALL_ROW;
            const auto &cats = app.session().live.categories();
            for (size_t i = 0; i < cats.size() && !categoryId.empty(); i++) {
                if (cats[i].id == categoryId) {
                    row = FIRST_CATEGORY_ROW + (int) i;
                }
            }
            categoryList->setSelected(row);
            currentCategoryRow = -1;
            lastInCategory[row] = keep;
            selectCategory(row);
            channelList->reload();
            updateNotice();
            requestLogos(true);
        }

        std::string selectedStreamId() const {
            const LiveChannel *c = selectedChannel();
            return c ? c->id : std::string();
        }

        void selectCategory(int row, bool force = false) {
            if (row == currentCategoryRow && !force) {
                return;
            }
            // remember where we were in the category we leave
            if (currentCategoryRow >= 0 && !visible.empty()) {
                lastInCategory[currentCategoryRow] = selectedStreamId();
            }
            std::string keep = force ? selectedStreamId() : lastInCategory[row];
            currentCategoryRow = row;
            const LiveCatalog &live = app.session().live;
            std::string caption;
            if (row == FAVORITES_ROW) {
                visible = live.favorites(app.library().favorites(ContentType::Live));
                caption = tr("live.favorites_caption");
            } else if (row == ALL_ROW) {
                visible.resize(live.channels().size());
                for (size_t i = 0; i < visible.size(); i++) {
                    visible[i] = (int) i;
                }
                caption = tr("live.all_caption");
            } else {
                const Category &cat = live.categories()[(size_t) (row - FIRST_CATEGORY_ROW)];
                visible = live.inCategory(cat.id);
                caption = live.categoryName(cat.id);
            }
            openCategoryId = row >= FIRST_CATEGORY_ROW ? live.categories()[(size_t) (row - FIRST_CATEGORY_ROW)].id
                                                       : std::string();
            channelCaption->setText(visible.empty() ? caption : caption + "  \xC2\xB7  " + std::to_string(visible.size()));
            int index = 0;
            if (!keep.empty()) {
                for (size_t i = 0; i < visible.size(); i++) {
                    if (live.channels()[(size_t) visible[i]].id == keep) {
                        index = (int) i;
                        break;
                    }
                }
            }
            channelList->setSelected(index);
            if (!app.session().liveLoaded) {
                empty->setText(tr("live.list_failed"));
            } else if (visible.empty()) {
                empty->setText(tr(row == FAVORITES_ROW ? "live.no_favorites" : "live.empty_category"));
            } else {
                empty->setText("");
            }
            if (visible.empty() && focus == 1) {
                setFocus(0);
            }
            refreshDetails();
        }

        void setFocus(int f) {
            focus = f;
            categoryList->setFocused(focus == 0);
            channelList->setFocused(focus == 1);
            screens::Hints h;
            if (focus == 0) {
                h = {{ui::Glyph::Cross, tr("common.open")}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, tr("live.category")},
                     {ui::Glyph::L2, ""}, {ui::Glyph::R2, tr("common.page")}, {ui::Glyph::Triangle, tr("home.search")}};
            } else {
                h = {{ui::Glyph::Cross, tr("live.watch")}, {ui::Glyph::Square, tr("common.favorite")},
                     {ui::Glyph::L2, ""}, {ui::Glyph::R2, tr("common.page")}, {ui::Glyph::L1, ""},
                     {ui::Glyph::R1, tr("live.category")}, {ui::Glyph::Triangle, tr("home.search")}};
            }
            if (playlist) {
                h.push_back({ui::Glyph::Options, tr("m3u.options")});
            }
            h.push_back({ui::Glyph::Circle, tr(focus == 0 ? "common.back" : "live.categories")});
            hints->setHints(h);
            refreshDetails();
        }

        const LiveChannel *selectedChannel() const {
            if (visible.empty()) {
                return nullptr;
            }
            int i = std::min(std::max(channelList->selected(), 0), (int) visible.size() - 1);
            return &app.session().live.channels()[(size_t) visible[(size_t) i]];
        }

        // the selection moved: logos for what is on screen now; the prefetch window once it settles
        void moved(bool repeating) {
            lastMoveAt = app.now();
            prefetchPending = true;
            requestLogos(!repeating);
        }

        void requestLogos(bool prefetch) {
            std::vector<std::string> urls;
            if (app.settings().get().loadImages && !visible.empty()) {
                const auto &channels = app.session().live.channels();
                int n = (int) visible.size();
                auto add = [&](int i) {
                    if (i >= 0 && i < n) {
                        urls.push_back(channels[(size_t) visible[(size_t) i]].icon);
                    }
                };
                add(channelList->selected());
                int first = channelList->firstVisible();
                int last = first + channelList->visibleCount() - 1;
                for (int i = first; i <= last; i++) {
                    add(i);
                }
                for (int d = 1; prefetch && d <= PREFETCH_ROWS; d++) {
                    add(last + d);
                    add(first - d);
                }
            }
            app.images().want(urls);
        }

        void toggleFavorite() {
            const LiveChannel *c = selectedChannel();
            bool on = app.library().toggleFavorite(ContentType::Live, c->id);
            app.saveLibrary();
            app.toast(tr(on ? "favorites.added" : "favorites.removed"), on ? ToastKind::Success : ToastKind::Info);
            if (currentCategoryRow == FAVORITES_ROW) {
                selectCategory(FAVORITES_ROW, true);
            } else {
                channelList->reload();
            }
            categoryList->reload();
            refreshDetails();
        }

        void play(int index) {
            std::weak_ptr<bool> alive = aliveToken;
            app.push(screens::makeLivePlayer(app, visible, index, [this, alive](const std::string &streamId) {
                if (alive.lock()) {
                    returnStreamId = streamId;
                }
            }));
        }

        CategoriesAdapter categoriesAdapter;
        ChannelsAdapter channelsAdapter;
        ui::ListView *categoryList;
        ui::ListView *channelList;
        ui::Label *channelCaption;
        ui::Label *empty;
        RectangleShape *pane = nullptr;
        ui::LogoView *detailLogo = nullptr;
        ui::Label *detailName = nullptr;
        ui::Label *detailCategory = nullptr;
        ui::Label *detailNumber = nullptr;
        ui::Label *detailFav = nullptr;
        RectangleShape *watch = nullptr;
        ui::HintBar *hints;
        std::vector<int> visible;
        std::unordered_map<int, std::string> lastInCategory;   // category row -> last selected stream id
        std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);
        int currentCategoryRow = -1;
        int focus = 0;
        std::string returnStreamId;
        std::string focusedId;        // the selected channel (survives a catalog replacement)
        std::string openCategoryId;   // "" for Favorites / All Channels
        ui::Label *notice = nullptr;
        bool playlist = false;
        unsigned generation = 0;
        unsigned logoGeneration = 0;
        double lastMoveAt = 0;
        bool prefetchPending = true;
    };
}

namespace screens {
    Screen *makeLive(App &app, bool startOnFavorites) {
        return new LiveScreen(app, startOnFavorites);
    }
}
