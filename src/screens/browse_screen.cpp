// Movies and Series browsers (one template): a quiet category rail | a grid of large posters.
//
// Opening a browser loads its catalog lazily through VodLibrary (saved copy first, provider refresh in the
// background). The grid is virtualized (7 columns, 2 whole rows and a third that peeks out under the bottom
// fade); posters come from ImageLoader (kind Poster): the cells on screen first, then one row above and below
// once scrolling pauses. Geometry: ui/layout.h (layout::catalog), docs/v7-ui-v2-layouts.md.
//
// The header line names the open category with its count and order. The focused poster grows a little, gets
// the accent outline and full brightness (the others are dimmed), and shows its metadata under its title.
// Options opens Sort / Refresh catalog / Catalog info. The order applies inside the selected category and is
// kept per profile for Movies and Series separately; "Recently added" and "Continue watching" keep their own.

#include <unordered_map>

#include "common.h"
#include "../platform/clock.h"
#include "../core/format.h"
#include "../platform/log.h"

using namespace c2d;
using namespace iptv;

namespace {

    const int ALL_ROW = 0;
    const int RECENT_ROW = 1;
    const int CONTINUE_ROW = 2;
    const int FAVORITES_ROW = 3;
    const int FIRST_CATEGORY_ROW = 4;

    namespace L = layout::catalog;

    const size_t RECENT_LIMIT = 200;
    const double PREFETCH_PAUSE = 0.30;

    struct MovieTraits {
        using Item = Movie;
        using Catalog = MovieCatalog;
        static constexpr ContentType TYPE = ContentType::Movie;
        static const int SEARCH_FILTER = 1;

        static const char *heading() { return "movies"; }   // screen name (log) i18n-exempt

        // localization keys
        static const char *headingKey() { return "home.movies"; }

        static const char *allKey() { return "browse.movies_all"; }

        static const char *countKey() { return "home.movie_count"; }

        static const char *loadingKey() { return "browse.movies_loading"; }

        static const char *failedKey() { return "browse.movies_failed"; }

        static const char *sortKey() { return "browse.movies_sort"; }

        static const char *updatedKey() { return "browse.movies_updated"; }

        static const char *optionsKey() { return "browse.movies_options"; }

        static const char *infoKey() { return "browse.movies_info"; }

        static bool downloaded(App &a, const Item &m) {
            dl::Item d;
            return a.downloads().findCompleted(a.session().profile.id, dl::Kind::Movie, m.streamId, d);
        }

        static const Catalog &catalog(App &a) { return a.vod().movies(); }

        static const SectionStatus &status(App &a) { return a.vod().movieStatus(); }

        static void open(App &a) { a.vod().openMovies(a.session().profile); }

        static void refresh(App &a) { a.vod().refreshMovies(a.session().profile); }

        static const std::string &id(const Item &m) { return m.streamId; }

        static const std::string &title(const Item &m) { return m.title; }

        static const std::string &image(const Item &m) { return m.icon; }

        static std::string meta(App &a, const Item &m) {
            std::string s;
            auto add = [&s](const std::string &part) {
                if (!part.empty()) {
                    s += (s.empty() ? "" : "  \xC2\xB7  ") + part;
                }
            };
            add(m.year > 0 ? std::to_string(m.year) : "");
            add(fmt::rating(m.rating));
            const HistoryEntry *p = a.library().progressOf(TYPE, m.streamId);
            if (p && p->watched) {
                add("\xE2\x9C\x93 " + tr("progress.watched"));
            } else if (p && progress::inProgress(p->position, p->duration, p->watched)) {
                add(fmt::remaining(p->position, p->duration));
            }
            return s;
        }

        static float progressOf(App &a, const Item &m, bool &watched) {
            const HistoryEntry *p = a.library().progressOf(TYPE, m.streamId);
            watched = p && p->watched;
            return p && progress::inProgress(p->position, p->duration, p->watched)
                   ? std::max(0.02f, (float) progress::fraction(p->position, p->duration)) : 0.0f;
        }

        static std::vector<std::string> continueIds(App &a) {
            std::vector<std::string> ids;
            for (const HistoryEntry *e: a.library().continueWatching(100)) {
                if (e->type == TYPE) {
                    ids.push_back(e->id);
                }
            }
            return ids;
        }

        static Screen *detail(App &a, const Item &m) { return screens::makeMovieDetail(a, m); }
    };

    struct SeriesTraits {
        using Item = Series;
        using Catalog = SeriesCatalog;
        static constexpr ContentType TYPE = ContentType::Series;
        static const int SEARCH_FILTER = 2;

        static const char *heading() { return "series"; }   // screen name (log) i18n-exempt

        static const char *headingKey() { return "home.series"; }

        static const char *allKey() { return "browse.series_all"; }

        static const char *countKey() { return "home.series_count"; }

        static const char *loadingKey() { return "browse.series_loading"; }

        static const char *failedKey() { return "browse.series_failed"; }

        static const char *sortKey() { return "browse.series_sort"; }

        static const char *updatedKey() { return "browse.series_updated"; }

        static const char *optionsKey() { return "browse.series_options"; }

        static const char *infoKey() { return "browse.series_info"; }

        // any episode of the series downloaded
        static bool downloaded(App &a, const Item &s) {
            return a.downloads().hasCompletedEpisodes(a.session().profile.id, s.seriesId);
        }

        static const Catalog &catalog(App &a) { return a.vod().series(); }

        static const SectionStatus &status(App &a) { return a.vod().seriesStatus(); }

        static void open(App &a) { a.vod().openSeries(a.session().profile); }

        static void refresh(App &a) { a.vod().refreshSeries(a.session().profile); }

        static const std::string &id(const Item &s) { return s.seriesId; }

        static const std::string &title(const Item &s) { return s.title; }

        static const std::string &image(const Item &s) { return s.cover; }

        static std::string meta(App &, const Item &s) {
            std::string out;
            auto add = [&out](const std::string &part) {
                if (!part.empty()) {
                    out += (out.empty() ? "" : "  \xC2\xB7  ") + part;
                }
            };
            add(s.year > 0 ? std::to_string(s.year) : "");
            add(fmt::rating(s.rating));
            add(s.genre);
            return out;
        }

        static float progressOf(App &, const Item &, bool &watched) {
            watched = false;
            return 0;
        }

        static std::vector<std::string> continueIds(App &a) {
            std::vector<std::string> ids;
            for (const HistoryEntry *e: a.library().continueWatching(100)) {
                if (e->type == TYPE && !e->seriesId.empty()) {
                    ids.push_back(e->seriesId);
                }
            }
            return ids;
        }

        static Screen *detail(App &a, const Item &s) { return screens::makeSeriesDetail(a, s); }
    };

    template<typename Traits>
    class BrowseScreen : public Screen {
        using Item = typename Traits::Item;

    public:
        explicit BrowseScreen(App &a) : Screen(a), categoriesAdapter(this), gridAdapter(this) {
            ui::background(this);
            ui::Label *title = screens::header(this, tr(Traits::headingKey()));
            context = new screens::HeaderContext(this, title);
            statusLabel = screens::headerStatus(this);

            categoryList = new ui::ListView(FloatRect(layout::RAIL_X, L::GRID_Y, layout::RAIL_W, L::RAIL_H),
                                            layout::RAIL_ROW_H, layout::RAIL_ROW_GAP, &categoriesAdapter);
            add(categoryList);
            grid = new ui::GridView(FloatRect(L::GRID_X, L::GRID_Y, L::GRID_W, L::GRID_H), L::POSTER_W, L::CELL_H,
                                    L::COLUMNS, L::ROWS, &gridAdapter, L::PEEK_ROWS);
            add(grid);
            // the peek row and the rail's last rows dissolve before the hints
            screens::bottomFade(this);

            const float gw = L::GRID_W - L::SCROLL_GUTTER;
            stateText = ui::label(this, "", theme::BODY, L::GRID_X, L::GRID_Y + 260, ui::Weight::Regular,
                                  theme::textMuted());
            stateText->setAlign(ui::Align::Center, gw);
            stateText->setMaxWidth(gw - 160);
            stateText->setMaxLines(3);
            spinner = new ui::Spinner(18);
            spinner->setPosition(L::GRID_X + (gw - 18 * 3.6f) / 2, L::GRID_Y + 380);
            add(spinner);

            hints = screens::hintBar(this, {});
            sortMode = app.settings().sortMode(app.session().profile.id, Traits::TYPE);
            categoryList->setSelected(ALL_ROW);
            setFocus(0);
            Traits::open(app);
            sync(true);
        }

        const char *name() const override { return Traits::heading(); }

        void onPause() override {
            app.images().want(std::vector<ImageRequest>());
        }

        void onResume() override {
            // favorites / progress may have changed in a detail screen or the player
            sync(true);
            requestImages(true);
        }

        void tick(double now) override {
            if (app.vod().generation() != vodGeneration) {
                sync(false);
                announceRefresh();
                redraw();
            }
            if (app.images().generation() != imageGeneration) {
                imageGeneration = app.images().generation();
                grid->reload();
                redraw();
            }
            if (spinner->isVisible() && spinner->tick(now)) {
                redraw();
            }
            if (prefetchPending && now - lastMoveAt >= PREFETCH_PAUSE) {
                prefetchPending = false;
                requestImages(true);
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                case PadButton::Down: {
                    int d = e.button == PadButton::Up ? -1 : 1;
                    if (focus == 0) {
                        if (categoryList->moveSelection(d)) {
                            selectCategory(categoryList->selected());
                            moved(e.repeat);
                        }
                    } else if (grid->navigate(0, d)) {
                        moved(e.repeat);
                    }
                    return;
                }
                case PadButton::Left:
                    if (focus == 1 && !grid->navigate(-1, 0)) {
                        setFocus(0);   // first column: back to the categories
                    } else if (focus == 1) {
                        moved(e.repeat);
                    }
                    return;
                case PadButton::Right:
                    if (focus == 0) {
                        if (!visible.empty()) {
                            setFocus(1);
                        }
                    } else if (grid->navigate(1, 0)) {
                        moved(e.repeat);
                    }
                    return;
                case PadButton::L2:
                case PadButton::R2: {
                    int d = e.button == PadButton::L2 ? -1 : 1;
                    if (focus == 0) {
                        if (categoryList->moveSelection(d * categoryList->pageSize())) {
                            selectCategory(categoryList->selected());
                            moved(e.repeat);
                        }
                    } else if (grid->page(d)) {
                        moved(e.repeat);
                    }
                    return;
                }
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
                        } else if (Traits::status(app).status == CatalogStatus::Failed) {
                            Traits::open(app);   // retry
                        }
                    } else if (const Item *it = selectedItem()) {
                        app.push(Traits::detail(app, *it));
                    }
                    return;
                case PadButton::Square:
                    if (!e.repeat && focus == 1) {
                        if (const Item *it = selectedItem()) {
                            bool on = app.library().toggleFavorite(Traits::TYPE, Traits::id(*it));
                            app.saveLibrary();
                            app.toast(tr(on ? "favorites.added" : "favorites.removed"),
                                      on ? ToastKind::Success : ToastKind::Info);
                            if (currentRow == FAVORITES_ROW) {
                                selectCategory(FAVORITES_ROW, true);
                            }
                            categoryList->reload();
                        }
                    }
                    return;
                case PadButton::Triangle:
                    if (!e.repeat) {
                        app.push(screens::makeSearch(app, Traits::SEARCH_FILTER));
                    }
                    return;
                case PadButton::Options:
                    if (!e.repeat) {
                        openOptions();
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
        }

    private:
        // ------------------------------------------------------------------ adapters
        struct CategoriesAdapter : ui::ListView::Adapter {
            explicit CategoriesAdapter(BrowseScreen *s) : screen(s) {}

            int count() override {
                return FIRST_CATEGORY_ROW + (int) Traits::catalog(screen->app).categories().size();
            }

            C2DObject *createRow(float w, float h) override {
                Row r;
                r.bg = new ui::RowFrame(w, h, -1);   // the rail has no dividers: it stays quiet next to the grid
                r.name = ui::label(r.bg, "", theme::LABEL, layout::ROW_PAD, ui::Label::centerOffset(theme::LABEL, h));
                r.name->setMaxWidth(w - layout::ROW_PAD - 64);
                r.count = ui::label(r.bg, "", theme::CAPTION, 0, ui::Label::centerOffset(theme::CAPTION, h),
                                    ui::Weight::Regular, theme::textMuted());
                r.count->setAlign(ui::Align::Right, w - 14);
                rows.push_back(r);
                return r.bg;
            }

            void bindRow(C2DObject *obj, int index, bool selected, bool focused) override {
                // rest: quiet (muted while the grid has the focus); the open category: accent edge + semibold;
                // focused: the accent-tinted surface
                bool railFocused = screen->focus == 0;
                for (auto &r: rows) {
                    if (r.bg != obj) {
                        continue;
                    }
                    int n = 0;
                    std::string name = screen->rowName(index, n);
                    r.count->setText(n >= 0 ? std::to_string(n) : "");
                    // the name takes what the count leaves (most counts are two or three digits)
                    r.name->setMaxWidth(r.bg->getSize().x - layout::ROW_PAD - (n >= 0 ? r.count->width() + 26 : 14));
                    r.name->setText(name);
                    r.bg->setState(focused, selected);
                    r.name->setWeight(selected ? ui::Weight::SemiBold : ui::Weight::Regular);
                    r.name->setColor(focused ? theme::textStrong() : selected ? theme::textPrimary()
                                     : railFocused ? theme::textSecondary() : theme::textMuted());
                    r.count->setColor(focused ? theme::textSecondary() : theme::textMuted());
                }
            }

            struct Row {
                ui::RowFrame *bg;
                ui::Label *name;
                ui::Label *count;
            };
            BrowseScreen *screen;
            std::vector<Row> rows;
        };

        struct GridAdapter : ui::GridView::Adapter {
            explicit GridAdapter(BrowseScreen *s) : screen(s) {}

            int count() override { return (int) screen->visible.size(); }

            C2DObject *createCell(float w, float h) override {
                Cell c;
                c.root = new RectangleShape(FloatRect(0, 0, w, h));
                c.root->setFillColor(theme::none());
                c.poster = new ui::PosterView(FloatRect(0, 0, w, L::POSTER_H), theme::LABEL);
                c.poster->setCornersRadius(theme::RADIUS);
                c.poster->setFocusScale(L::POSTER_FOCUS_SCALE);
                c.root->add(c.poster);
                const float ty = L::POSTER_H + L::TITLE_GAP;
                c.title = ui::label(c.root, "", theme::LABEL, 0, ty, ui::Weight::Regular, theme::textMuted());
                c.title->setAlign(ui::Align::Center, w);
                c.title->setMaxWidth(w);
                // the focused poster's facts may use part of the gaps beside it
                const float extra = 28;
                c.meta = ui::label(c.root, "", theme::CAPTION, -extra / 2, ty + 32, ui::Weight::Regular,
                                   theme::textSecondary());
                c.meta->setAlign(ui::Align::Center, w + extra);
                c.meta->setMaxWidth(w + extra);
                cells.push_back(c);
                return c.root;
            }

            void bindCell(C2DObject *obj, int index, bool focused) override {
                const Item &it = Traits::catalog(screen->app).items()[(size_t) screen->visible[(size_t) index]];
                for (auto &c: cells) {
                    if (c.root != obj) {
                        continue;
                    }
                    std::shared_ptr<ImageSet> img = screen->app.images().get(ImageKind::Poster, Traits::image(it));
                    c.poster->set(Traits::title(it), img ? img->at(0).texture : nullptr,
                                  img ? img->at(0).size : Vector2i());
                    bool watched = false;
                    c.poster->setProgress(Traits::progressOf(screen->app, it, watched));
                    c.poster->setWatched(watched);
                    c.poster->setDownloaded(Traits::downloaded(screen->app, it));
                    c.poster->setFocused(focused);
                    c.poster->setDimmed(!focused);
                    c.title->setText(Traits::title(it));
                    c.title->setColor(focused ? theme::textStrong() : theme::textMuted());
                    c.title->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                    c.meta->setText(focused ? Traits::meta(screen->app, it) : "");
                }
            }

            struct Cell {
                RectangleShape *root;
                ui::PosterView *poster;
                ui::Label *title;
                ui::Label *meta;
            };
            BrowseScreen *screen;
            std::vector<Cell> cells;
        };

        // ------------------------------------------------------------------ data
        std::string rowName(int row, int &count) {
            const auto &cat = Traits::catalog(app);
            switch (row) {
                case ALL_ROW:
                    count = (int) cat.size();
                    return tr(Traits::allKey());
                case RECENT_ROW:
                    count = -1;
                    return tr("browse.recently_added");
                case CONTINUE_ROW:
                    count = continueCount;
                    return tr("browse.continue");
                case FAVORITES_ROW:
                    count = (int) cat.favorites(app.library().favorites(Traits::TYPE)).size();
                    return "\xE2\x98\x85  " + tr("home.favorites");
                default: {
                    const Category &c = cat.categories()[(size_t) (row - FIRST_CATEGORY_ROW)];
                    count = (int) cat.inCategory(c.id).size();
                    return c.name;
                }
            }
        }

        const Item *selectedItem() const {
            if (visible.empty()) {
                return nullptr;
            }
            int i = std::min(std::max(grid->selected(), 0), (int) visible.size() - 1);
            return &Traits::catalog(app).items()[(size_t) visible[(size_t) i]];
        }

        std::string selectedId() const {
            const Item *it = selectedItem();
            return it ? Traits::id(*it) : std::string();
        }

        // the order chosen in Options, inside the category (Recently added / Continue watching keep theirs)
        std::vector<int> sorted(std::vector<int> list) {
            const auto &cat = Traits::catalog(app);
            if (sortMode == SortMode::Provider || !cat.sortSupport().supports(sortMode)) {
                return list;
            }
            double t0 = clockx::monotonic();
            if (sortMode == SortMode::RecentlyWatched) {
                std::vector<int64_t> activity(cat.size(), 0);
                for (const auto &a: app.library().lastActivity(Traits::TYPE)) {
                    int i = cat.indexOf(a.first);
                    if (i >= 0) {
                        activity[(size_t) i] = a.second;
                    }
                }
                cat.sort(list, sortMode, &activity);
            } else {
                cat.sort(list, sortMode);
            }
            LOG_V("browse", "%s: %d items sorted (%s) in %.1f ms", Traits::heading(), (int) list.size(),
                  sortModeKey(sortMode), (clockx::monotonic() - t0) * 1000);
            return list;
        }

        std::vector<int> itemsOf(int row) {
            const auto &cat = Traits::catalog(app);
            switch (row) {
                case ALL_ROW:
                    return sorted(cat.all());
                case RECENT_ROW:
                    return cat.recent(RECENT_LIMIT);
                case CONTINUE_ROW: {
                    std::vector<int> out;
                    for (const auto &id: Traits::continueIds(app)) {
                        if (const Item *it = cat.find(id)) {
                            out.push_back((int) (it - cat.items().data()));
                        }
                    }
                    return out;
                }
                case FAVORITES_ROW:
                    return sorted(cat.favorites(app.library().favorites(Traits::TYPE)));
                default:
                    if (row - FIRST_CATEGORY_ROW < (int) cat.categories().size()) {
                        return sorted(cat.inCategory(cat.categories()[(size_t) (row - FIRST_CATEGORY_ROW)].id));
                    }
                    return {};
            }
        }

        void selectCategory(int row, bool force = false) {
            if (row == currentRow && !force) {
                return;
            }
            if (currentRow >= 0 && !visible.empty()) {
                lastInCategory[currentRow] = selectedId();
            }
            std::string keep = force ? selectedId() : lastInCategory[row];
            currentRow = row;
            visible = itemsOf(row);
            int index = 0;
            if (!keep.empty()) {
                const auto &items = Traits::catalog(app).items();
                for (size_t i = 0; i < visible.size(); i++) {
                    if (Traits::id(items[(size_t) visible[i]]) == keep) {
                        index = (int) i;
                        break;
                    }
                }
            }
            grid->setSelected(index);
            if (row >= FIRST_CATEGORY_ROW && row - FIRST_CATEGORY_ROW < (int) Traits::catalog(app).categories().size()) {
                currentCategoryId = Traits::catalog(app).categories()[(size_t) (row - FIRST_CATEGORY_ROW)].id;
            } else {
                currentCategoryId.clear();
            }
            refreshState();
            refreshInfo();
        }

        // the catalog or its status changed (first load, refresh, sign-in)
        void sync(bool force) {
            vodGeneration = app.vod().generation();
            const auto &cat = Traits::catalog(app);
            bool catalogChanged = catalogSize != cat.size() || catalogSavedAt != cat.savedAt;
            if (catalogChanged || force) {
                catalogSize = cat.size();
                catalogSavedAt = cat.savedAt;
                if (!cat.sortSupport().supports(sortMode)) {
                    sortMode = SortMode::Provider;   // this catalog has no such metadata
                }
                continueCount = (int) itemsOf(CONTINUE_ROW).size();
                int row = std::min(categoryList->selected(), categoriesAdapter.count() - 1);
                if (catalogChanged && !currentCategoryId.empty()) {
                    // a refreshed list may order its categories differently: stay in the same one
                    row = ALL_ROW;
                    for (size_t i = 0; i < cat.categories().size(); i++) {
                        if (cat.categories()[i].id == currentCategoryId) {
                            row = FIRST_CATEGORY_ROW + (int) i;
                        }
                    }
                }
                categoryList->setSelected(row);
                selectCategory(row, true);
                if (catalogChanged && !cat.empty()) {
                    requestImages(true);
                }
            }
            refreshState();
        }

        void refreshState() {
            const SectionStatus &st = Traits::status(app);
            const auto &cat = Traits::catalog(app);
            bool loading = st.status == CatalogStatus::Loading || st.status == CatalogStatus::NotLoaded;
            std::string text;
            if (loading && cat.empty()) {
                text = tr(Traits::loadingKey())
                       + (st.status == CatalogStatus::Loading ? "\n" + tr("browse.first_time") : "");
            } else if (st.status == CatalogStatus::Failed) {
                text = tr(Traits::failedKey(), {st.message}) + "\n" + tr("browse.press_x_retry");
            } else if (visible.empty()) {
                text = tr(currentRow == FAVORITES_ROW ? "browse.no_favorites"
                          : currentRow == CONTINUE_ROW ? "browse.nothing_continue" : "browse.empty_category");
            }
            stateText->setText(text);
            spinner->setVisibility(loading && cat.empty() ? Visibility::Visible : Visibility::Hidden);

            // the counts are on the header line already: the status only says where the list comes from
            std::string status;
            if (!cat.empty()) {
                status = st.refreshing ? tr("browse.refreshing") : st.fromCache ? tr("browse.saved_list") : "";
            }
            if (!st.message.empty() && !cat.empty()) {
                status = st.message;
            }
            statusLabel->setText(status);
            statusLabel->setColor(!st.message.empty() ? theme::warning() : theme::textMuted());
        }

        static std::string withThousands(size_t n) {
            std::string digits = std::to_string(n);
            std::string out;
            for (size_t i = 0; i < digits.size(); i++) {
                if (i > 0 && (digits.size() - i) % 3 == 0) {
                    out += ',';
                }
                out += digits[i];
            }
            return out;
        }

        void refreshInfo() {
            // the header line: the open category, complete, with its count and order
            int dummy = 0;
            int row = currentRow < 0 ? ALL_ROW : currentRow;
            std::string name = row < categoriesAdapter.count() ? rowName(row, dummy) : "";
            std::string meta;
            if (!Traits::catalog(app).empty()) {
                meta = i18n::count(Traits::countKey(), (long long) visible.size());
                bool ownOrder = row == RECENT_ROW || row == CONTINUE_ROW;
                if (!ownOrder) {
                    meta += "   \xC2\xB7   " + std::string(sortModeName(sortMode, Traits::TYPE));
                }
            }
            context->set(Traits::catalog(app).empty() ? "" : name, meta);
        }

        // ------------------------------------------------------------------ options
        void openOptions() {
            std::vector<std::string> options = {tr("browse.sort_value", {sortModeName(sortMode, Traits::TYPE)}),
                                                tr("browse.refresh"), tr("browse.catalog_info")};
            std::vector<std::string> details = {tr("browse.sort_detail"), tr("browse.refresh_detail"),
                                                tr("browse.catalog_info_detail")};
            app.push(screens::makeMenu(app, tr(Traits::optionsKey()), options, -1,
                                       guardedChoice([this](int c) {
                                           if (c == 0) {
                                               openSortMenu();
                                           } else if (c == 1) {
                                               refreshCatalog();
                                           } else if (c == 2) {
                                               showDiagnostics();
                                           }
                                       }), details));
        }

        template<typename F>
        std::function<void(int)> guardedChoice(F f) {
            std::weak_ptr<bool> w = aliveToken;
            return [w, f](int c) {
                if (w.lock()) {
                    f(c);
                }
            };
        }

        void openSortMenu() {
            const SortSupport &support = Traits::catalog(app).sortSupport();
            std::vector<SortMode> modes;
            std::vector<std::string> names;
            int checked = 0;
            for (int i = 0; i < (int) SortMode::Count; i++) {
                SortMode m = (SortMode) i;
                if (!support.supports(m)) {
                    continue;   // only orders the provider metadata supports
                }
                if (m == sortMode) {
                    checked = (int) modes.size();
                }
                modes.push_back(m);
                names.push_back(sortModeName(m, Traits::TYPE));
            }
            app.push(screens::makeMenu(app, tr(Traits::sortKey()), names, checked,
                                       guardedChoice([this, modes](int c) {
                                           if (c < 0 || c >= (int) modes.size() || modes[(size_t) c] == sortMode) {
                                               return;
                                           }
                                           sortMode = modes[(size_t) c];
                                           app.settings().setSortMode(app.session().profile.id, Traits::TYPE, sortMode);
                                           std::string err;
                                           if (!app.settings().save(&err)) {
                                               LOG_E("settings", "save failed: %s", err.c_str());
                                           }
                                           selectCategory(currentRow < 0 ? ALL_ROW : currentRow, true);
                                           grid->setSelected(0);   // the new order starts at its top
                                           refreshInfo();
                                           requestImages(true);
                                       })));
        }

        void refreshCatalog() {
            refreshRequested = true;
            refreshSeen = Traits::status(app).refreshes;
            Traits::refresh(app);
            app.toast(tr("browse.refreshing"));
            refreshState();
        }

        // after a refresh started from Options: "Catalog updated" or what went wrong
        void announceRefresh() {
            const SectionStatus &st = Traits::status(app);
            if (!refreshRequested || st.refreshes == refreshSeen || st.refreshing) {
                return;
            }
            refreshRequested = false;
            if (st.lastRefreshOk) {
                app.toast(tr(Traits::updatedKey(), {i18n::count(Traits::countKey(), (long long) Traits::catalog(app).size())}),
                          ToastKind::Success);
            } else {
                app.toast(tr("browse.refresh_failed", {st.lastRefreshError}), ToastKind::Error);
            }
        }

        void showDiagnostics() {
            const auto &cat = Traits::catalog(app);
            const CatalogDiagnostics &d = cat.diagnostics();
            const SectionStatus &st = Traits::status(app);
            auto n = [](int v) { return v < 0 ? std::string("-") : withThousands((size_t) v); };
            std::string sep = "   \xC2\xB7   ";
            std::string text;
            if (cat.empty()) {
                text = tr("browse.info_not_loaded");
            } else {
                (void) sep;
                text = tr("browse.info_counts", {n(d.parse.raw), n(d.parse.parsed), n(d.cached)})
                       + "\n" + tr("browse.info_visible", {n(d.visible), n(d.indexed), n(d.uncategorized), n(d.dropped())})
                       + "\n" + tr("browse.info_dropped", {n(d.parse.rejectedMissingId), n(d.parse.rejectedDuplicateId),
                                                           n(d.parse.rejectedNotObject)})
                       + "\n" + tr("browse.info_kept", {n(d.parse.missingName), n(d.parse.missingCategory),
                                                        n(d.parse.missingPoster)})
                       + "\n" + tr(st.fromCache ? "browse.info_saved" : "browse.info_downloaded",
                                   {n(d.categories), clockx::localDate(cat.savedAt)})
                       + "\n" + tr("browse.info_timing", {std::to_string((int) d.parseMs), std::to_string((int) d.indexMs),
                                                          std::to_string((int) (d.indexBytes / 1024))});
            }
            app.push(screens::makeDialog(app, tr(Traits::infoKey()), text, {tr("common.close")}, nullptr));
        }

        void setFocus(int f) {
            focus = f;
            categoryList->setFocused(focus == 0);
            grid->setFocused(focus == 1);
            if (focus == 0) {
                hints->setHints({{ui::Glyph::Cross, tr("common.open")}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, tr("live.category")},
                                 {ui::Glyph::Triangle, tr("home.search")}, {ui::Glyph::Options, tr("browse.sort")},
                                 {ui::Glyph::Circle, tr("common.back")}});
            } else {
                hints->setHints({{ui::Glyph::Cross, tr("browse.details")}, {ui::Glyph::Square, tr("common.favorite")},
                                 {ui::Glyph::L1, ""}, {ui::Glyph::R1, tr("live.category")},
                                 {ui::Glyph::Options, tr("browse.sort")}, {ui::Glyph::Circle, tr("live.categories")}});
            }
            categoryList->reload();   // rail texts step back while the grid has the focus
            refreshInfo();
        }

        void moved(bool repeating) {
            refreshInfo();
            lastMoveAt = app.now();
            prefetchPending = true;
            requestImages(!repeating);
        }

        void requestImages(bool prefetch) {
            std::vector<ImageRequest> req;
            if (app.settings().get().loadImages && !visible.empty()) {
                const auto &items = Traits::catalog(app).items();
                int n = (int) visible.size();
                auto add = [&](int i) {
                    if (i >= 0 && i < n) {
                        req.push_back({ImageKind::Poster, Traits::image(items[(size_t) visible[(size_t) i]])});
                    }
                };
                add(grid->selected());
                int first = grid->firstVisible();
                int last = first + grid->cellCount() - 1;
                for (int i = first; i <= last; i++) {
                    add(i);
                }
                for (int d = 1; prefetch && d <= L::COLUMNS; d++) {   // one row below and above
                    add(last + d);
                    add(first - d);
                }
            }
            app.images().want(req);
        }

        CategoriesAdapter categoriesAdapter;
        GridAdapter gridAdapter;
        ui::ListView *categoryList;
        ui::GridView *grid;
        screens::HeaderContext *context;
        ui::Label *statusLabel;
        ui::Label *stateText;
        ui::Spinner *spinner;
        ui::HintBar *hints;
        std::vector<int> visible;
        std::unordered_map<int, std::string> lastInCategory;
        int currentRow = -1;
        int focus = 0;
        int continueCount = 0;
        size_t catalogSize = (size_t) -1;
        int64_t catalogSavedAt = -1;
        unsigned vodGeneration = 0;
        unsigned imageGeneration = 0;
        double lastMoveAt = 0;
        bool prefetchPending = true;
        SortMode sortMode = SortMode::Provider;
        std::string currentCategoryId;
        bool refreshRequested = false;
        unsigned refreshSeen = 0;
        std::shared_ptr<bool> aliveToken = std::make_shared<bool>(true);
    };
}

namespace screens {
    Screen *makeMovies(App &app) {
        return new BrowseScreen<MovieTraits>(app);
    }

    Screen *makeSeries(App &app) {
        return new BrowseScreen<SeriesTraits>(app);
    }
}
