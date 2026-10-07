// Search across Movies, Series and Live TV, from the prebuilt per-catalog indexes (iptv/search_index.h):
// nothing is normalized per keypress and no server request is made.
//
// Left: the query and an on-screen keyboard (typing searches as you go, 200 ms after the last key).
// Right: All / Movies / Series / Live TV filters with their counts (L1 / R1), the result count and the
// results with their type and poster / logo. Opening Search loads the Movies / Series catalogs lazily
// (saved copy first) when needed; results appear as each catalog becomes ready.

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

    const float LEFT_X = theme::SAFE_X;
    const float LEFT_W = 652;
    const float FIELD_Y = 138;
    const float FIELD_H = 80;
    const float KEYS_Y = 244;
    const float KEY_UNIT = 47;
    const float KEY_GAP = 8;
    const float KEY_H = 64;
    const float RIGHT_X = 800;
    const float RIGHT_W = theme::SCREEN_W - theme::SAFE_X - RIGHT_X;
    const float CHIP_W = 236;
    const float CHIP_H = 56;
    const float LIST_Y = 262;
    const float LIST_H = 712;
    const float ROW_H = 104;

    const char *FILTER_NAMES[] = {"All", "Movies", "Series", "Live TV"};

    struct Result {
        ContentType type;
        int index;   // into the catalog of that type
        int score;
    };

    class SearchScreen : public Screen, public ui::ListView::Adapter {
    public:
        SearchScreen(App &a, int initialFilter) : Screen(a), keyboard("", 120, KeyboardModel::Layout::Search),
                                                  filter(initialFilter < 0 || initialFilter > 3 ? 0 : initialFilter) {
            ui::background(this);
            screens::header(this, "Search");

            // query + keyboard
            field = ui::box(this, FloatRect(LEFT_X, FIELD_Y, LEFT_W, FIELD_H), theme::surface(), theme::RADIUS_SMALL);
            field->setOutlineColor(theme::accent());
            queryText = ui::label(field, "", theme::HEADING, 24, ui::Label::centerOffset(theme::HEADING, FIELD_H));
            queryText->setMaxWidth(LEFT_W - 48);
            const auto &rows = keyboard.rows();
            for (int r = 0; r < (int) rows.size(); r++) {
                std::vector<KeyView> line;
                for (int c = 0; c < (int) rows[(size_t) r].size(); c++) {
                    const Key &k = rows[(size_t) r][(size_t) c];
                    float x = LEFT_X + (float) keyboard.keyStart(r, c) * (KEY_UNIT + KEY_GAP);
                    float w = (float) k.span * KEY_UNIT + (float) (k.span - 1) * KEY_GAP;
                    KeyView v;
                    v.bg = ui::box(this, FloatRect(x, KEYS_Y + (float) r * (KEY_H + KEY_GAP), w, KEY_H),
                                   theme::surfaceRaised(), 10);
                    bool special = k.action != KeyAction::Char;
                    unsigned size = special ? theme::CAPTION : theme::BODY;
                    v.label = ui::label(v.bg, keyboard.label(k), size, 0, ui::Label::centerOffset(size, KEY_H),
                                        ui::Weight::SemiBold);
                    v.label->setAlign(ui::Align::Center, w);
                    line.push_back(v);
                }
                keys.push_back(line);
            }
            float below = KEYS_Y + (float) rows.size() * (KEY_H + KEY_GAP) + 24;
            catalogsText = ui::label(this, "", theme::LABEL, LEFT_X, below, ui::Weight::Regular, theme::textDim());
            catalogsText->setMaxWidth(LEFT_W);
            catalogsText->setMaxLines(4);

            // filters + results
            for (int i = 0; i < 4; i++) {
                chips[i] = ui::box(this, FloatRect(RIGHT_X + (float) i * (CHIP_W + 16), FIELD_Y, CHIP_W, CHIP_H),
                                   theme::surface(), CHIP_H / 2);
                chipText[i] = ui::label(chips[i], "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, CHIP_H),
                                        ui::Weight::SemiBold);
                chipText[i]->setAlign(ui::Align::Center, CHIP_W);
            }
            summary = ui::label(this, "", theme::BODY, RIGHT_X, 214, ui::Weight::Regular, theme::textDim());
            summary->setMaxWidth(RIGHT_W);
            list = new ui::ListView(FloatRect(RIGHT_X, LIST_Y, RIGHT_W, LIST_H), ROW_H, 8, this);
            add(list);
            empty = ui::label(this, "", theme::BODY, RIGHT_X, LIST_Y + 200, ui::Weight::Regular, theme::textMuted());
            empty->setAlign(ui::Align::Center, ui::ListView::rowWidth(RIGHT_W));
            empty->setMaxWidth(RIGHT_W - 80);
            empty->setMaxLines(3);
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
            r.bg = ui::box(nullptr, FloatRect(0, 0, w, h), theme::surface(), theme::RADIUS_SMALL);
            r.poster = new ui::PosterView(FloatRect(18, 7, 60, 90), 14);
            r.bg->add(r.poster);
            r.logo = new ui::LogoView(FloatRect(14, (h - 64) / 2, 112, 64), 22, 4, 1.25f);
            r.bg->add(r.logo);
            r.title = ui::label(r.bg, "", theme::BODY, 148, ui::Label::centerOffset(theme::BODY, h) - 16,
                                ui::Weight::SemiBold);
            r.title->setMaxWidth(w - 148 - 24);
            r.meta = ui::label(r.bg, "", theme::LABEL, 148, ui::Label::centerOffset(theme::LABEL, h) + 20,
                               ui::Weight::Regular, theme::textDim());
            r.meta->setMaxWidth(w - 148 - 24);
            rows.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int index, bool, bool focused) override {
            const Result &res = shown[(size_t) index];
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                r.bg->setFillColor(focused ? theme::rowFocus() : theme::surface());
                r.bg->setOutlineColor(theme::accent());
                r.bg->setOutlineThickness(focused ? 3 : 0);
                r.title->setColor(focused ? Color::White : theme::text());
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
                    r.meta->setText(category.empty() ? "Live TV" : "Live TV" + sep + category);
                } else if (res.type == ContentType::Movie) {
                    const Movie &m = app.vod().movies().items()[(size_t) res.index];
                    std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Poster, m.icon);
                    r.poster->set(m.title, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                    r.title->setText(m.title);
                    std::string meta = "Movie";
                    if (m.year > 0) {
                        meta += sep + std::to_string(m.year);
                    }
                    if (m.rating > 0) {
                        meta += sep + fmt::rating(m.rating);
                    }
                    r.meta->setText(meta);
                } else {
                    const Series &se = app.vod().series().items()[(size_t) res.index];
                    std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Poster, se.cover);
                    r.poster->set(se.title, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                    r.title->setText(se.title);
                    std::string meta = "Series";
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
                        setFilter((filter + (e.button == PadButton::L1 ? 3 : 1)) % 4);
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
            RectangleShape *bg;
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
                        setZone(1);
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
                    setZone(0);
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
            for (const auto &c: app.session().live.categories()) {
                if (c.id == id) {
                    return c.name;
                }
            }
            return "";
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

        static std::string sectionState(const SectionStatus &st, size_t n, const char *noun) {
            if (n > 0) {
                return countText((int) n) + " " + noun;
            }
            switch (st.status) {
                case CatalogStatus::Failed:
                    return std::string(noun) + " unavailable";
                case CatalogStatus::Ready:
                    return std::string("no ") + noun;
                default:
                    return std::string(noun) + " loading" "\xE2\x80\xA6";
            }
        }

        void refreshKeys() {
            const auto &rows = keyboard.rows();
            for (int r = 0; r < (int) rows.size(); r++) {
                for (int c = 0; c < (int) rows[(size_t) r].size(); c++) {
                    const Key &k = rows[(size_t) r][(size_t) c];
                    KeyView &v = keys[(size_t) r][(size_t) c];
                    bool focused = zone == 0 && r == keyboard.focusRow() && c == keyboard.focusCol();
                    v.bg->setFillColor(focused ? theme::accent() : k.action == KeyAction::Ok ? Color(30, 74, 140)
                                                                 : k.action == KeyAction::Char ? theme::surfaceRaised()
                                                                 : theme::surfaceFocus());
                    v.bg->setOutlineColor(Color::White);
                    v.bg->setOutlineThickness(focused ? 3 : 0);
                }
            }
        }

        void refresh() {
            const std::string &q = keyboard.text();
            queryText->setText(q.empty() ? "Type a title or channel" : q + (zone == 0 ? "|" : ""));
            queryText->setColor(q.empty() ? theme::textMuted() : theme::text());
            field->setOutlineThickness(zone == 0 ? 2 : 0);
            refreshKeys();

            bool searched = !search::normalize(lastQuery).empty();
            for (int i = 0; i < 4; i++) {
                bool sel = i == filter;
                chips[i]->setFillColor(sel ? theme::accentDark() : theme::surface());
                chipText[i]->setText(searched ? std::string(FILTER_NAMES[i]) + "  " + countText(totals[i])
                                              : FILTER_NAMES[i]);
                chipText[i]->setColor(sel ? Color::White : theme::textDim());
            }
            int n = searched ? totals[filter] : 0;
            if (!searched) {
                summary->setText("");
            } else {
                std::string what = filter == 0 ? "" : std::string(" in ") + FILTER_NAMES[filter];
                summary->setText(countText(n) + (n == 1 ? " result" : " results") + what + " for \xE2\x80\x9C"
                                 + lastQuery + "\xE2\x80\x9D"
                                 + ((int) shown.size() < n ? "  (best " + countText((int) shown.size()) + " shown)" : ""));
            }
            const SectionStatus &ms = app.vod().movieStatus();
            const SectionStatus &ss = app.vod().seriesStatus();
            bool loading = ms.status == CatalogStatus::Loading || ss.status == CatalogStatus::Loading
                           || ms.status == CatalogStatus::NotLoaded || ss.status == CatalogStatus::NotLoaded;
            if (!searched) {
                empty->setText("Type to search movies, series and live channels.\n"
                               "Turkish letters, apostrophes and small typos are fine.");
            } else if (shown.empty()) {
                empty->setText("No results for \xE2\x80\x9C" + lastQuery + "\xE2\x80\x9D"
                               + (filter != 0 && totals[0] > 0 ? std::string(" in ") + FILTER_NAMES[filter] : "")
                               + (loading ? std::string("\nMovies and series are still loading" "\xE2\x80\xA6") : ""));
            } else {
                empty->setText("");
            }
            std::string sep = "   \xC2\xB7   ";
            catalogsText->setText("Searching " + sectionState(ms, app.vod().movies().size(), "movies") + sep
                                  + sectionState(ss, app.vod().series().size(), "series") + sep
                                  + (app.session().liveLoaded ? countText((int) app.session().live.channels().size())
                                                                + " channels" : std::string("channels unavailable")));
            list->reload();
            if (zone == 0) {
                hints->setHints({{ui::Glyph::Cross, "Type"}, {ui::Glyph::Square, "Delete"},
                                 {ui::Glyph::Triangle, "Space"}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, "Filter"},
                                 {ui::Glyph::R2, "Results"}, {ui::Glyph::Circle, "Back"}});
            } else {
                hints->setHints({{ui::Glyph::Cross, "Open"}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, "Filter"},
                                 {ui::Glyph::L2, ""}, {ui::Glyph::R2, "Page"}, {ui::Glyph::Square, "Delete"},
                                 {ui::Glyph::Circle, "Keyboard"}});
            }
        }

        KeyboardModel keyboard;
        int filter;
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
        ui::Label *catalogsText;
        RectangleShape *chips[4];
        ui::Label *chipText[4];
        ui::Label *summary;
        ui::ListView *list;
        ui::Label *empty;
        ui::HintBar *hints;
        std::vector<Row> rows;
    };
}

namespace screens {
    Screen *makeSearch(App &app, int filter) {
        return new SearchScreen(app, filter);
    }
}
