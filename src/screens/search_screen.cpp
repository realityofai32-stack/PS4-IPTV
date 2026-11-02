// Search across Movies, Series and Live TV, from the prebuilt per-catalog indexes (iptv/search_index.h):
// nothing is normalized per keypress and no server request is made.
//
// Top: the query in a full-width field. Left: a compact on-screen keyboard (typing searches as you go, 200 ms
// after the last key). Right, owning the rest of the screen: All / Movies / Series / Live TV filters with their
// counts (L1 / R1), the result count and the results with their type and poster / logo; before the first query
// a short left-aligned instruction sits where the results will be. Geometry: ui/layout.h (layout::search). Opening Search loads the Movies / Series catalogs lazily
// (saved copy first) when needed; results appear as each catalog becomes ready.
//
// Playlist (M3U) sources only have Live TV: the filters are All / Live TV. Search covers the source that is
// open (like Favorites and Recently Watched, which are per source). A refreshed playlist re-runs the query.

#include "common.h"
#include "../core/format.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../ui/keyboard_model.h"

using namespace c2d;
using namespace iptv;

namespace {

    const double DEBOUNCE = 0.2;
    const size_t MOVIE_LIMIT = 400;
    const size_t SERIES_LIMIT = 200;
    const size_t LIVE_LIMIT = 200;

    namespace L = layout::search;

    const float CHIP_GAP = 12;

    const char *FILTER_KEYS[] = {"search.filter_all", "home.movies", "home.series", "home.live_tv"};

    struct Result {
        ContentType type;
        int index;   // into the catalog of that type
        int score;
    };

    class SearchScreen : public Screen, public ui::ListView::Adapter {
    public:
        SearchScreen(App &a, int initialFilter) : Screen(a), keyboard("", 120, KeyboardModel::Layout::Search),
                                                  filter(initialFilter < 0 || initialFilter > 3 ? 0 : initialFilter) {
            playlist = app.session().profile.isPlaylist();
            filters = playlist ? std::vector<int>{0, 3} : std::vector<int>{0, 1, 2, 3};
            if (std::find(filters.begin(), filters.end(), filter) == filters.end()) {
                filter = 0;
            }
            liveGen = app.session().liveGeneration;
            ui::background(this);
            ui::Label *title = screens::header(this, tr("home.search"));
            context = new screens::HeaderContext(this, title);

            // query: one wide field over the keyboard and the results
            field = ui::box(this, FloatRect(theme::SAFE_X, L::FIELD_Y, theme::CONTENT_W, L::FIELD_H), theme::surface(),
                            theme::RADIUS);
            field->setOutlineColor(theme::accent());
            auto *icon = new ui::IconButton(ui::Icon::Search, 52);
            icon->setPosition(16, (L::FIELD_H - 52) / 2);
            field->add(icon);
            queryText = ui::label(field, "", theme::HEADING, 84, ui::Label::centerOffset(theme::HEADING, L::FIELD_H));
            queryText->setMaxWidth(theme::CONTENT_W - 84 - 32);
            const auto &rows = keyboard.rows();
            for (int r = 0; r < (int) rows.size(); r++) {
                std::vector<KeyView> line;
                for (int c = 0; c < (int) rows[(size_t) r].size(); c++) {
                    const Key &k = rows[(size_t) r][(size_t) c];
                    float x = L::KEYS_X + (float) keyboard.keyStart(r, c) * (L::KEY_UNIT + L::KEY_GAP);
                    float w = (float) k.span * L::KEY_UNIT + (float) (k.span - 1) * L::KEY_GAP;
                    KeyView v;
                    v.bg = ui::box(this, FloatRect(x, L::KEYS_Y + (float) r * (L::KEY_H + L::KEY_GAP), w, L::KEY_H),
                                   theme::surfaceElevated(), theme::RADIUS_SMALL);
                    bool special = k.action != KeyAction::Char;
                    unsigned size = special ? theme::CAPTION : theme::BODY;
                    v.label = ui::label(v.bg, keyboard.label(k), size, 0, ui::Label::centerOffset(size, L::KEY_H),
                                        ui::Weight::SemiBold);
                    v.label->setAlign(ui::Align::Center, w);
                    v.label->setMaxWidth(w - 8);
                    line.push_back(v);
                }
                keys.push_back(line);
            }

            // filters + results
            for (int i = 0; i < 4; i++) {
                chips[i] = new ui::Chip(tr(FILTER_KEYS[i]), L::RESULTS_X, L::CHIPS_Y, 0, L::CHIP_H);
                bool has = std::find(filters.begin(), filters.end(), i) != filters.end();
                chips[i]->setVisibility(has ? Visibility::Visible : Visibility::Hidden);
                add(chips[i]);
            }
            summary = ui::label(this, "", theme::LABEL, L::RESULTS_X, L::SUMMARY_Y, ui::Weight::Regular,
                                theme::textSecondary());
            summary->setMaxWidth(L::RESULTS_W);
            list = new ui::ListView(FloatRect(L::RESULTS_X, L::LIST_Y, L::RESULTS_W, L::LIST_H), L::ROW_H, L::ROW_GAP, this);
            add(list);
            empty = new ui::EmptyState(FloatRect(L::RESULTS_X, L::LIST_Y + 8, ui::ListView::rowWidth(L::RESULTS_W), 400),
                                       true);
            add(empty);
            hints = screens::hintBar(this, {});
            list->setFocused(false);
            refresh();
        }

        const char *name() const override { return "search"; }

        void onEnter() override {
            app.vod().openMovies(app.session().profile);
            app.vod().openSeries(app.session().profile);
            vodGen = app.vod().generation();
            refresh();
        }

        void onResume() override {
            if (app.session().liveGeneration != liveGen) {
                liveGen = app.session().liveGeneration;
                all.clear();   // live indices are stale
                shown.clear();
                if (!lastQuery.empty()) {
                    run(true);
                }
                refresh();
            }
            if (app.vod().generation() != vodGen) {
                // a catalog was refreshed meanwhile: the result indices are stale, search again first
                vodGen = app.vod().generation();
                if (!lastQuery.empty()) {
                    run(true);
                }
                refresh();
            }
            requestImages();
        }

        void onPause() override {
            app.images().want(std::vector<ImageRequest>());
        }

        void tick(double now) override {
            if (app.session().liveGeneration != liveGen) {
                // the playlist was refreshed: Live TV results point into the old list, search again
                liveGen = app.session().liveGeneration;
                all.clear();
                shown.clear();
                if (!lastQuery.empty()) {
                    run(true);
                }
                refresh();
                redraw();
            }
            if (dueAt > 0 && now >= dueAt) {
                dueAt = 0;
                run(false);
                redraw();
            }
            // a catalog finished loading or was refreshed: search again so its results appear
            if (app.vod().generation() != vodGen) {
                vodGen = app.vod().generation();
                if (!lastQuery.empty()) {
                    run(true);
                }
                refresh();
                redraw();
            }
            if (app.images().generation() != imageGen) {
                imageGen = app.images().generation();
                list->reload();
                redraw();
            }
        }

        // ------------------------------------------------------------------ results list
        int count() override { return (int) shown.size(); }

        C2DObject *createRow(float w, float h) override {
            Row r;
            r.bg = new ui::RowFrame(w, h, 0);
            r.poster = new ui::PosterView(FloatRect(20, (h - 84) / 2, 56, 84), 14);
            r.poster->setCornersRadius(theme::RADIUS_SMALL);
            r.bg->add(r.poster);
            r.logo = new ui::LogoView(FloatRect(16, (h - 54) / 2, 96, 54), 22, 4, 1.25f);
            r.logo->setCornersRadius(theme::RADIUS_SMALL);
            r.bg->add(r.logo);
            const float x = 136;
            r.title = ui::label(r.bg, "", theme::BODY, x, ui::Label::centerOffset(theme::BODY, h) - 16,
                                ui::Weight::SemiBold);
            r.title->setMaxWidth(w - x - 24);
            r.meta = ui::label(r.bg, "", theme::LABEL, x, ui::Label::centerOffset(theme::LABEL, h) + 18,
                               ui::Weight::Regular, theme::textSecondary());
            r.meta->setMaxWidth(w - x - 24);
            rows.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int index, bool, bool focused) override {
            const Result &res = shown[(size_t) index];
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                r.bg->setState(focused);
                r.title->setColor(focused ? theme::textStrong() : theme::textPrimary());
                bool live = res.type == ContentType::Live;
                r.poster->setVisibility(live ? Visibility::Hidden : Visibility::Visible);
                r.logo->setVisibility(live ? Visibility::Visible : Visibility::Hidden);
                std::string sep = " \xC2\xB7 ";
                if (live) {
                    const LiveChannel &c = app.session().live.channels()[(size_t) res.index];
                    std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Logo, c.icon);
                    r.logo->set(c.name, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                    r.title->setText(c.name);
                    std::string category = categoryName(c.categoryId);
                    r.meta->setText(category.empty() ? tr("home.kind_live") : tr("home.kind_live") + sep + category);
                } else if (res.type == ContentType::Movie) {
                    const Movie &m = app.vod().movies().items()[(size_t) res.index];
                    std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Poster, m.icon);
                    r.poster->set(m.title, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                    r.title->setText(m.title);
                    std::string meta = tr("home.kind_movie");
                    if (m.year > 0) {
                        meta += sep + std::to_string(m.year);
                    }
                    if (m.rating > 0) {
                        meta += sep + fmt::rating(m.rating);
                    }
                    dl::Item d;
                    if (app.downloads().findCompleted(app.session().profile.id, dl::Kind::Movie, m.streamId, d)) {
                        meta += sep + "\xE2\x86\x93 " + tr("download.badge");   // downloaded: plays offline
                    }
                    r.meta->setText(meta);
                } else {
                    const Series &se = app.vod().series().items()[(size_t) res.index];
                    std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Poster, se.cover);
                    r.poster->set(se.title, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                    r.title->setText(se.title);
                    std::string meta = tr("home.kind_series");
                    if (se.year > 0) {
                        meta += sep + std::to_string(se.year);
                    }
                    if (!se.genre.empty()) {
                        meta += sep + se.genre;
                    }
                    r.meta->setText(meta);
                }
            }
        }

        // ------------------------------------------------------------------ input
        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::L1:
                case PadButton::R1:
                    if (!e.repeat) {
                        int n = (int) filters.size();
                        int at = (int) (std::find(filters.begin(), filters.end(), filter) - filters.begin());
                        setFilter(filters[(size_t) ((at + (e.button == PadButton::L1 ? n - 1 : 1)) % n)]);
                    }
                    return;
                case PadButton::Square:
                    keyboard.backspace();
                    edited();
                    return;
                case PadButton::Triangle:
                    keyboard.space();
                    edited();
                    return;
                case PadButton::Circle:
                    if (e.repeat) {
                        return;
                    }
                    if (zone == 1) {
                        setZone(0);
                    } else {
                        app.pop();
                    }
                    return;
                default:
                    break;
            }
            if (zone == 0) {
                keyboardInput(e);
            } else {
                resultsInput(e);
            }
        }

    private:
        struct KeyView {
            RectangleShape *bg;
            ui::Label *label;
        };

        struct Row {
            ui::RowFrame *bg;
            ui::PosterView *poster;
            ui::LogoView *logo;
            ui::Label *title;
            ui::Label *meta;
        };

        void keyboardInput(const InputEvent &e) {
            switch (e.button) {
                case PadButton::Up:
                    keyboard.move(0, -1);
                    break;
                case PadButton::Down:
                    keyboard.move(0, 1);
                    break;
                case PadButton::Left:
                    keyboard.move(-1, 0);
                    break;
                case PadButton::Right:
                    // right edge of the keyboard: on to the results
                    if (keyboard.focusCol() + 1 == (int) keyboard.rows()[(size_t) keyboard.focusRow()].size()
                        && !shown.empty()) {
                        if (!e.repeat) {
                            setZone(1);
                        }
                        return;
                    }
                    keyboard.move(1, 0);
                    break;
                case PadButton::R2:
                    if (!e.repeat && !shown.empty()) {
                        flush();
                        setZone(1);
                    }
                    return;
                case PadButton::Cross: {
                    if (e.repeat) {
                        return;
                    }
                    std::string before = keyboard.text();
                    KeyboardModel::Result r = keyboard.press();
                    if (r == KeyboardModel::Result::Ok) {
                        flush();
                        if (!shown.empty()) {
                            setZone(1);
                        }
                        return;
                    }
                    if (keyboard.text() != before) {
                        edited();
                        return;
                    }
                    break;
                }
                default:
                    return;
            }
            refreshKeys();
        }

        void resultsInput(const InputEvent &e) {
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    break;
                case PadButton::Down:
                    list->moveSelection(1);
                    break;
                case PadButton::Left:
                    if (!e.repeat) {
                        setZone(0);   // a held direction scrolls, it never switches zones
                    }
                    return;
                case PadButton::L2:
                    list->moveSelection(-list->pageSize());
                    break;
                case PadButton::R2:
                    list->moveSelection(list->pageSize());
                    break;
                case PadButton::Cross:
                    if (!e.repeat && !shown.empty()) {
                        open(list->selected());
                    }
                    return;
                default:
                    return;
            }
            requestImages();
        }

        void setZone(int z) {
            zone = z;
            list->setFocused(zone == 1);
            refresh();
        }

        void setFilter(int f) {
            filter = f;
            buildShown();
            list->setSelected(0);
            if (zone == 1 && shown.empty()) {
                setZone(0);
            }
            refresh();
            requestImages();
        }

        // the query changed: search when typing pauses
        void edited() {
            dueAt = app.now() + DEBOUNCE;
            refresh();
        }

        // search now if a debounced query is waiting
        void flush() {
            if (dueAt > 0) {
                dueAt = 0;
                run(false);
            }
        }

        std::string categoryName(const std::string &id) const {
            return app.session().live.categoryName(id);   // Uncategorized in the current UI language
        }

        void run(bool keepSelection) {
            std::string query = keyboard.text();
            double t0 = clockx::monotonic();
            all.clear();
            totals[1] = totals[2] = totals[3] = 0;
            if (!search::normalize(query).empty()) {
                search::Result movies = app.vod().movies().searchRanked(query, MOVIE_LIMIT);
                search::Result series = app.vod().series().searchRanked(query, SERIES_LIMIT);
                search::Result live = app.session().live.searchRanked(query, LIVE_LIMIT);
                totals[1] = movies.total;
                totals[2] = series.total;
                totals[3] = live.total;
                for (const auto &h: movies.hits) {
                    all.push_back({ContentType::Movie, h.item, h.score});
                }
                for (const auto &h: series.hits) {
                    all.push_back({ContentType::Series, h.item, h.score});
                }
                for (const auto &h: live.hits) {
                    all.push_back({ContentType::Live, h.item, h.score});
                }
                // one ranking across types (each list is already best-first)
                std::stable_sort(all.begin(), all.end(), [](const Result &a, const Result &b) {
                    return a.score > b.score;
                });
            }
            totals[0] = totals[1] + totals[2] + totals[3];
            lastQuery = query;
            double ms = (clockx::monotonic() - t0) * 1000;
            LOG_V("search", "query of %d chars: %d movies, %d series, %d live in %.1f ms", (int) query.size(),
                  totals[1], totals[2], totals[3], ms);
            int keep = keepSelection ? list->selected() : 0;
            buildShown();
            list->setSelected(keep);
            if (zone == 1 && shown.empty()) {
                setZone(0);
            }
            refresh();
            requestImages();
        }

        void buildShown() {
            shown.clear();
            for (const auto &r: all) {
                if (filter == 0 || (filter == 1 && r.type == ContentType::Movie)
                    || (filter == 2 && r.type == ContentType::Series) || (filter == 3 && r.type == ContentType::Live)) {
                    shown.push_back(r);
                }
            }
        }

        void open(int index) {
            const Result &r = shown[(size_t) index];
            if (r.type == ContentType::Live) {
                // zap context: the live results of this search
                std::vector<int> liveResults;
                int position = 0;
                for (const auto &x: all) {
                    if (x.type == ContentType::Live) {
                        if (x.index == r.index) {
                            position = (int) liveResults.size();
                        }
                        liveResults.push_back(x.index);
                    }
                }
                app.push(screens::makeLivePlayer(app, liveResults, position));
            } else if (r.type == ContentType::Movie) {
                app.push(screens::makeMovieDetail(app, app.vod().movies().items()[(size_t) r.index]));
            } else {
                app.push(screens::makeSeriesDetail(app, app.vod().series().items()[(size_t) r.index]));
            }
        }

        void requestImages() {
            if (!app.settings().get().loadImages || shown.empty()) {
                app.images().want(std::vector<ImageRequest>());
                return;
            }
            std::vector<ImageRequest> req;
            int first = list->firstVisible();
            int last = std::min((int) shown.size(), first + list->visibleCount() + 2);
            for (int i = std::max(0, first - 1); i < last; i++) {
                const Result &r = shown[(size_t) i];
                if (r.type == ContentType::Live) {
                    req.push_back({ImageKind::Logo, app.session().live.channels()[(size_t) r.index].icon});
                } else if (r.type == ContentType::Movie) {
                    req.push_back({ImageKind::Poster, app.vod().movies().items()[(size_t) r.index].icon});
                } else {
                    req.push_back({ImageKind::Poster, app.vod().series().items()[(size_t) r.index].cover});
                }
            }
            app.images().want(req);
        }

        static std::string countText(int n) {
            return fmt::number(n);   // 19,797 / 19.797
        }

        // keyPrefix "search.movies" -> .count / .unavailable / .none / .loading
        static std::string sectionState(const SectionStatus &st, size_t n, const std::string &keyPrefix) {
            if (n > 0) {
                return tr((keyPrefix + ".count").c_str(), {countText((int) n)});
            }
            switch (st.status) {
                case CatalogStatus::Failed:
                    return tr((keyPrefix + ".unavailable").c_str());
                case CatalogStatus::Ready:
                    return tr((keyPrefix + ".none").c_str());
                default:
                    return tr((keyPrefix + ".loading").c_str());
            }
        }

        void refreshKeys() {
            const auto &rows = keyboard.rows();
            for (int r = 0; r < (int) rows.size(); r++) {
                for (int c = 0; c < (int) rows[(size_t) r].size(); c++) {
                    const Key &k = rows[(size_t) r][(size_t) c];
                    KeyView &v = keys[(size_t) r][(size_t) c];
                    bool focused = zone == 0 && r == keyboard.focusRow() && c == keyboard.focusCol();
                    // while the results have focus the keyboard steps back: flat keys, muted letters
                    bool typing = zone == 0;
                    v.bg->setFillColor(focused ? theme::accent() : !typing ? theme::surface()
                                                                 : k.action == KeyAction::Ok ? theme::accentMuted()
                                                                 : k.action == KeyAction::Char ? theme::surfaceElevated()
                                                                 : theme::cardHover());
                    v.bg->setOutlineThickness(0);
                    v.label->setColor(focused ? theme::onAccent() : typing ? theme::textPrimary() : theme::textMuted());
                }
            }
        }

        void refresh() {
            const std::string &q = keyboard.text();
            queryText->setText(q.empty() ? tr("search.placeholder") : q + (zone == 0 ? "|" : ""));
            queryText->setColor(q.empty() ? theme::textMuted() : theme::textPrimary());
            field->setOutlineThickness(zone == 0 ? theme::ROW_FOCUS_BORDER : 0);
            refreshKeys();

            bool searched = !search::normalize(lastQuery).empty();
            // chips fitted to their text, in the order this source has them
            float cx = L::RESULTS_X;
            for (int f: filters) {
                chips[f]->setText(searched ? tr(FILTER_KEYS[f]) + "  " + countText(totals[f]) : tr(FILTER_KEYS[f]));
                chips[f]->setState(f == filter, false);
                chips[f]->setPosition(cx, L::CHIPS_Y);
                cx += chips[f]->width() + CHIP_GAP;
            }
            int n = searched ? totals[filter] : 0;
            if (!searched) {
                summary->setText("");
            } else {
                std::string text = filter == 0 ? tr(n == 1 ? "search.summary.one" : "search.summary.other", {countText(n), lastQuery})
                                               : tr(n == 1 ? "search.summary_in.one" : "search.summary_in.other",
                                                    {countText(n), lastQuery, tr(FILTER_KEYS[filter])});
                if ((int) shown.size() < n) {
                    text += "  " + tr("search.best_shown", {countText((int) shown.size())});
                }
                summary->setText(text);
            }
            const SectionStatus &ms = app.vod().movieStatus();
            const SectionStatus &ss = app.vod().seriesStatus();
            bool loading = !playlist && (ms.status == CatalogStatus::Loading || ss.status == CatalogStatus::Loading
                                         || ms.status == CatalogStatus::NotLoaded || ss.status == CatalogStatus::NotLoaded);
            if (!searched) {
                empty->set(tr("empty.search_intro_title"), tr("search.intro"));
            } else if (shown.empty()) {
                empty->set(filter != 0 && totals[0] > 0 ? tr("search.no_results_in", {lastQuery, tr(FILTER_KEYS[filter])})
                                                        : tr("search.no_results", {lastQuery}),
                           loading ? tr("search.still_loading") : tr("empty.search_none_hint"));
            } else {
                empty->set("", "");
            }
            std::string sep = "   \xC2\xB7   ";
            std::string channels = app.session().liveLoaded
                                   ? tr("search.channels.count", {countText((int) app.session().live.channels().size())})
                                   : tr("search.channels.unavailable");
            // what is searched, on the header line
            if (playlist) {
                context->set("", tr("search.searching", {channels}));
            } else {
                context->set("", tr("search.searching", {sectionState(ms, app.vod().movies().size(), "search.movies") + sep
                                + sectionState(ss, app.vod().series().size(), "search.series") + sep + channels}));
            }
            list->reload();
            if (zone == 0) {
                // the essentials (R2 still jumps to the results; so does Right at the keyboard's edge)
                hints->setHints({{ui::Glyph::Cross, tr("keyboard.hint_type")}, {ui::Glyph::Square, tr("common.delete")},
                                 {ui::Glyph::Triangle, tr("keyboard.hint_space")}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, tr("search.filter")},
                                 {ui::Glyph::Circle, tr("common.back")}});
            } else {
                hints->setHints({{ui::Glyph::Cross, tr("common.open")}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, tr("search.filter")},
                                 {ui::Glyph::L2, ""}, {ui::Glyph::R2, tr("common.page")}, {ui::Glyph::Circle, tr("search.keyboard")}});
            }
        }

        KeyboardModel keyboard;
        int filter;
        bool playlist = false;
        std::vector<int> filters;   // the filters this source has (0 All, 1 Movies, 2 Series, 3 Live TV)
        unsigned liveGen = 0;
        int zone = 0;            // 0 keyboard, 1 results
        double dueAt = 0;        // debounced search time (0 = none waiting)
        std::string lastQuery;   // what the results are for
        std::vector<Result> all;
        std::vector<Result> shown;
        int totals[4] = {0, 0, 0, 0};
        unsigned vodGen = 0;
        unsigned imageGen = 0;
        RectangleShape *field;
        ui::Label *queryText;
        std::vector<std::vector<KeyView>> keys;
        screens::HeaderContext *context;
        ui::Chip *chips[4];
        ui::Label *summary;
        ui::ListView *list;
        ui::EmptyState *empty;
        ui::HintBar *hints;
        std::vector<Row> rows;
    };
}

namespace screens {
    Screen *makeSearch(App &app, int filter) {
        return new SearchScreen(app, filter);
    }
}
