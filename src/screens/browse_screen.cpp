// Movies and Series browsers: categories | poster grid.
//
// Opening a browser loads its catalog lazily through VodLibrary (saved copy first, provider refresh in the
// background). The grid is virtualized (8 x 3 cells exist); posters come from ImageLoader (kind Poster):
// the cells on screen first, then one row above and below once scrolling pauses.
//
// Above the grid: the full name of the selected category with its count and order, and the complete title
// of the focused poster (captions under posters stay truncated). Options opens Sort / Refresh catalog /
// Catalog info. The order applies inside the selected category and is kept per profile for Movies and
// Series separately; "Recently added" and "Continue watching" keep their own order.

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

    const float TOP = 206;
    const float LIST_H = 770;
    const float CAT_X = theme::SAFE_X;
    const float CAT_W = 420;
    const float GRID_X = CAT_X + CAT_W + 32;
    const float GRID_W = theme::SCREEN_W - theme::SAFE_X - GRID_X;
    const int COLUMNS = 8;
    const int ROWS = 3;
    const float CELL_W = 140;
    const float POSTER_H = 210;
    const float CELL_H = 246;
    const size_t RECENT_LIMIT = 200;
    const double PREFETCH_PAUSE = 0.30;

    struct MovieTraits {
        using Item = Movie;
        using Catalog = MovieCatalog;
        static constexpr ContentType TYPE = ContentType::Movie;
        static const int SEARCH_FILTER = 1;

        static const char *heading() { return "Movies"; }

        static const char *noun() { return "movies"; }

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
                    s += (s.empty() ? "" : "   \xC2\xB7   ") + part;
                }
            };
            add(m.year > 0 ? std::to_string(m.year) : "");
            add(fmt::rating(m.rating));
            const HistoryEntry *p = a.library().progressOf(TYPE, m.streamId);
            if (p && p->watched) {
                add("\xE2\x9C\x93 Watched");
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

        static const char *heading() { return "Series"; }

        static const char *noun() { return "series"; }

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
                    out += (out.empty() ? "" : "   \xC2\xB7   ") + part;
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
            screens::header(this, Traits::heading());
            statusLabel = ui::label(this, "", theme::LABEL, 0, theme::SAFE_Y + 16, ui::Weight::Regular,
                                    theme::textDim());
            statusLabel->setAlign(ui::Align::Right, theme::SCREEN_W - theme::SAFE_X);
            statusLabel->setMaxWidth(1100);
            // above the grid: the selected category in full, then the focused title in full
            categoryTitle = ui::label(this, "", theme::BODY, GRID_X, 120, ui::Weight::SemiBold, theme::accent());
            categoryTitle->setMaxWidth(GRID_W - 300);
            categoryMeta = ui::label(this, "", theme::LABEL, GRID_X, 123, ui::Weight::Regular, theme::textDim());
            infoTitle = ui::label(this, "", theme::BODY, GRID_X, 160, ui::Weight::SemiBold);
            infoMeta = ui::label(this, "", theme::LABEL, 0, 163, ui::Weight::Regular, theme::textDim());
            infoMeta->setAlign(ui::Align::Right, GRID_X + ui::ListView::rowWidth(GRID_W));
            infoMeta->setMaxWidth(420);

            categoryList = new ui::ListView(FloatRect(CAT_X, TOP, CAT_W, LIST_H), 58, 6, &categoriesAdapter);
            add(categoryList);
            grid = new ui::GridView(FloatRect(GRID_X, TOP, GRID_W, LIST_H), CELL_W, CELL_H, COLUMNS, ROWS,
                                    &gridAdapter);
            add(grid);

            const float gw = ui::ListView::rowWidth(GRID_W);
            stateText = ui::label(this, "", theme::BODY, GRID_X, TOP + 300, ui::Weight::Regular, theme::textMuted());
            stateText->setAlign(ui::Align::Center, gw);
            stateText->setMaxWidth(gw - 160);
            stateText->setMaxLines(3);
            spinner = new ui::Spinner(18);
            spinner->setPosition(GRID_X + (gw - 18 * 3.6f) / 2, TOP + 400);
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
                            app.toast(on ? "Added to favorites" : "Removed from favorites",
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
                r.bg = ui::box(nullptr, FloatRect(0, 0, w, h), Color::Transparent, theme::RADIUS_SMALL);
                r.marker = ui::box(r.bg, FloatRect(0, 14, 4, h - 28), theme::accent(), 2);
                r.name = ui::label(r.bg, "", theme::LABEL + 2, 20, ui::Label::centerOffset(theme::LABEL + 2, h));
                r.name->setMaxWidth(w - 20 - 72);
                r.count = ui::label(r.bg, "", theme::CAPTION, 0, ui::Label::centerOffset(theme::CAPTION, h),
                                    ui::Weight::Regular, theme::textMuted());
                r.count->setAlign(ui::Align::Right, w - 16);
                rows.push_back(r);
                return r.bg;
            }

            void bindRow(C2DObject *obj, int index, bool selected, bool focused) override {
                for (auto &r: rows) {
                    if (r.bg != obj) {
                        continue;
                    }
                    int n = 0;
                    r.name->setText(screen->rowName(index, n));
                    r.count->setText(n >= 0 ? std::to_string(n) : "");
                    r.bg->setFillColor(focused ? theme::rowFocus() : selected ? theme::surface() : Color::Transparent);
                    r.bg->setOutlineColor(theme::accent());
                    r.bg->setOutlineThickness(focused ? 3 : 0);
                    r.marker->setVisibility(selected && !focused ? Visibility::Visible : Visibility::Hidden);
                    r.name->setWeight(selected ? ui::Weight::SemiBold : ui::Weight::Regular);
                    r.name->setColor(focused ? Color::White : selected ? theme::text() : theme::textDim());
                }
            }

            struct Row {
                RectangleShape *bg;
                RectangleShape *marker;
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
                c.root->setFillColor(Color::Transparent);
                c.poster = new ui::PosterView(FloatRect(0, 0, w, POSTER_H), theme::CAPTION);
                c.root->add(c.poster);
                c.title = ui::label(c.root, "", theme::CAPTION, 0, POSTER_H + 8, ui::Weight::Regular, theme::textDim());
                c.title->setAlign(ui::Align::Center, w);
                c.title->setMaxWidth(w);
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
                    c.poster->setFocused(focused);
                    c.title->setText(Traits::title(it));
                    c.title->setColor(focused ? Color::White : theme::textDim());
                    c.title->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                }
            }

            struct Cell {
                RectangleShape *root;
                ui::PosterView *poster;
                ui::Label *title;
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
                    return std::string("All ") + Traits::noun();
                case RECENT_ROW:
                    count = -1;
                    return "Recently added";
                case CONTINUE_ROW:
                    count = continueCount;
                    return "Continue watching";
                case FAVORITES_ROW:
                    count = (int) cat.favorites(app.library().favorites(Traits::TYPE)).size();
                    return "\xE2\x98\x85  Favorites";
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
                text = std::string("Loading ") + Traits::noun() + "\xE2\x80\xA6"
                       + (st.status == CatalogStatus::Loading ? "\nThe first time this can take a moment." : "");
            } else if (st.status == CatalogStatus::Failed) {
                text = std::string("The ") + Traits::noun() + " list could not be loaded: " + st.message
                       + "\nPress X on the categories to try again.";
            } else if (visible.empty()) {
                text = currentRow == FAVORITES_ROW ? "No favorites yet. Press the square button on a poster."
                       : currentRow == CONTINUE_ROW ? "Nothing to continue yet." : "Nothing in this category.";
            }
            stateText->setText(text);
            spinner->setVisibility(loading && cat.empty() ? Visibility::Visible : Visibility::Hidden);

            std::string status;
            if (!cat.empty()) {
                status = std::to_string(cat.size()) + " " + Traits::noun();
                if (st.refreshing) {
                    status += "   \xC2\xB7   refreshing" "\xE2\x80\xA6";
                } else if (st.fromCache) {
                    status += "   \xC2\xB7   saved list";
                }
            }
            if (!st.message.empty() && !cat.empty()) {
                status = st.message;
            }
            statusLabel->setText(status);
            statusLabel->setColor(!st.message.empty() ? theme::warning() : theme::textDim());
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
            // the selected category, complete, with its count and order
            int dummy = 0;
            int row = currentRow < 0 ? ALL_ROW : currentRow;
            categoryTitle->setText(row < categoriesAdapter.count() ? rowName(row, dummy) : "");
            std::string meta = withThousands(visible.size()) + " " + Traits::noun();
            bool ownOrder = row == RECENT_ROW || row == CONTINUE_ROW;
            if (!ownOrder) {
                meta += "   \xC2\xB7   " + std::string(sortModeName(sortMode, Traits::TYPE));
            }
            categoryMeta->setText(Traits::catalog(app).empty() ? "" : meta);
            categoryMeta->setPosition(GRID_X + categoryTitle->width() + 24, 123);

            const Item *it = focus == 1 ? selectedItem() : nullptr;
            if (it == nullptr) {
                infoTitle->setText("");
                infoMeta->setText("");
                return;
            }
            // the focused title in full: a smaller size when it does not fit, then two lines' worth of room
            std::string m = Traits::meta(app, *it);
            infoMeta->setText(m);
            float room = ui::ListView::rowWidth(GRID_W) - (m.empty() ? 0 : infoMeta->width() + 32);
            infoTitle->setMaxWidth(0);
            infoTitle->setCharSize(theme::BODY);
            infoTitle->setText(Traits::title(*it));
            if (infoTitle->width() > room) {
                infoTitle->setCharSize(theme::LABEL);
            }
            infoTitle->setMaxWidth(room);
        }

        // ------------------------------------------------------------------ options
        void openOptions() {
            std::vector<std::string> options = {std::string("Sort: ") + sortModeName(sortMode, Traits::TYPE),
                                                "Refresh catalog", "Catalog info"};
            std::vector<std::string> details = {"Order inside the selected category",
                                                "Download the provider list again (the current list stays meanwhile)",
                                                "Provider, parsed, cached, visible and indexed counts"};
            app.push(screens::makeMenu(app, std::string(Traits::heading()) + " options", options, -1,
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
            app.push(screens::makeMenu(app, "Sort " + std::string(Traits::noun()), names, checked,
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
            app.toast("Refreshing" "\xE2\x80\xA6");
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
                app.toast("Catalog updated: " + withThousands(Traits::catalog(app).size()) + " " + Traits::noun(),
                          ToastKind::Success);
            } else {
                app.toast("Refresh failed: " + st.lastRefreshError + ". The current list is kept.", ToastKind::Error);
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
                text = "The list is not loaded yet.";
            } else {
                text = "Provider " + n(d.parse.raw) + sep + "Parsed " + n(d.parse.parsed) + sep + "Cached " + n(d.cached)
                       + "\nVisible " + n(d.visible) + sep + "Indexed " + n(d.indexed) + sep + "Uncategorized "
                       + n(d.uncategorized) + sep + "Dropped " + n(d.dropped())
                       + "\nDropped by reason: no id " + n(d.parse.rejectedMissingId) + ", repeated id "
                       + n(d.parse.rejectedDuplicateId) + ", not an entry " + n(d.parse.rejectedNotObject)
                       + "\nKept without: name " + n(d.parse.missingName) + ", category " + n(d.parse.missingCategory)
                       + ", poster " + n(d.parse.missingPoster)
                       + "\n" + n(d.categories) + " categories" + sep
                       + (st.fromCache ? "saved list from " : "downloaded ") + clockx::localDate(cat.savedAt)
                       + "\nParse " + std::to_string((int) d.parseMs) + " ms" + sep + "search index "
                       + std::to_string((int) d.indexMs) + " ms, " + std::to_string((int) (d.indexBytes / 1024)) + " KB";
            }
            app.push(screens::makeDialog(app, std::string(Traits::heading()) + ": catalog info", text, {"Close"},
                                         nullptr));
        }

        void setFocus(int f) {
            focus = f;
            categoryList->setFocused(focus == 0);
            grid->setFocused(focus == 1);
            if (focus == 0) {
                hints->setHints({{ui::Glyph::Cross, "Open"}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, "Category"},
                                 {ui::Glyph::Triangle, "Search"}, {ui::Glyph::Options, "Sort & refresh"},
                                 {ui::Glyph::Circle, "Back"}});
            } else {
                hints->setHints({{ui::Glyph::Cross, "Details"}, {ui::Glyph::Square, "Favorite"},
                                 {ui::Glyph::L2, ""}, {ui::Glyph::R2, "Page"}, {ui::Glyph::L1, ""},
                                 {ui::Glyph::R1, "Category"}, {ui::Glyph::Triangle, "Search"},
                                 {ui::Glyph::Options, "Sort"}, {ui::Glyph::Circle, "Categories"}});
            }
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
                for (int d = 1; prefetch && d <= COLUMNS; d++) {   // one row below and above
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
        ui::Label *statusLabel;
        ui::Label *categoryTitle;
        ui::Label *categoryMeta;
        ui::Label *infoTitle;
        ui::Label *infoMeta;
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
