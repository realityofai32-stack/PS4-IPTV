// Global search over locally cached catalogs (no server request per keystroke): Live channels, then movies,
// then series. Opening Search loads the Movies / Series catalogs lazily (saved copy first) if needed.

#include "common.h"
#include "../core/format.h"

using namespace c2d;
using namespace iptv;

namespace {

    struct Result {
        ContentType type;
        int index;   // into the catalog of that type
    };

    class SearchScreen : public Screen, public ui::ListView::Adapter {
    public:
        explicit SearchScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, "Search");
            auto *field = ui::box(this, FloatRect(theme::SAFE_X, 150, 1200, 84), theme::surface(),
                                  theme::RADIUS_SMALL);
            queryText = ui::label(field, "", theme::HEADING, 28, ui::Label::centerOffset(theme::HEADING, 84));
            queryText->setMaxWidth(1140);
            summary = ui::label(this, "", theme::LABEL, 1330, 178, ui::Weight::Regular, theme::textDim());
            summary->setMaxWidth(500);
            list = new ui::ListView(FloatRect(theme::SAFE_X, 270, 1728, 700), 84, 8, this);
            add(list);
            empty = ui::label(this, "", theme::BODY, 0, 500, ui::Weight::Regular, theme::textMuted());
            empty->setAlign(ui::Align::Center, theme::SCREEN_W);
            empty->setMaxWidth(1200);
            empty->setMaxLines(2);
            screens::hintBar(this, {{ui::Glyph::Cross, "Open"}, {ui::Glyph::Triangle, "New search"},
                                    {ui::Glyph::Circle, "Back"}});
            refresh();
        }

        const char *name() const override { return "search"; }

        void onEnter() override {
            app.vod().openMovies(app.session().profile);
            app.vod().openSeries(app.session().profile);
            vodGen = app.vod().generation();
            edit();
        }

        void tick(double) override {
            // a catalog finished loading: search again so its results appear
            if (app.vod().generation() != vodGen) {
                vodGen = app.vod().generation();
                if (!query.empty()) {
                    int keep = list->selected();
                    run();
                    list->setSelected(keep);
                    redraw();
                }
            }
        }

        int count() override { return (int) results.size(); }

        C2DObject *createRow(float w, float h) override {
            auto *bg = ui::box(nullptr, FloatRect(0, 0, w, h), theme::surface(), theme::RADIUS_SMALL);
            auto *badge = ui::box(bg, FloatRect(20, (h - 40) / 2, 110, 40), theme::accentDark(), 20);
            auto *badgeText = ui::label(badge, "", theme::CAPTION, 0, ui::Label::centerOffset(theme::CAPTION, 40),
                                        ui::Weight::SemiBold);
            badgeText->setAlign(ui::Align::Center, 110);
            auto *n = ui::label(bg, "", theme::BODY, 160, ui::Label::centerOffset(theme::BODY, h), ui::Weight::SemiBold);
            n->setMaxWidth(w - 160 - 460);
            auto *meta = ui::label(bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h),
                                   ui::Weight::Regular, theme::textDim());
            meta->setAlign(ui::Align::Right, w - 24);
            meta->setMaxWidth(420);
            rows.push_back({bg, badge, badgeText, n, meta});
            return bg;
        }

        void bindRow(C2DObject *obj, int index, bool, bool focused) override {
            const Result &r = results[(size_t) index];
            for (auto &row: rows) {
                if (row.bg != obj) {
                    continue;
                }
                row.bg->setFillColor(focused ? theme::surfaceFocus() : theme::surface());
                row.bg->setOutlineColor(theme::accent());
                row.bg->setOutlineThickness(focused ? theme::FOCUS_BORDER : 0);
                if (r.type == ContentType::Live) {
                    const LiveChannel &c = app.session().live.channels()[(size_t) r.index];
                    row.badgeText->setText("TV");
                    row.badge->setFillColor(theme::accentDark());
                    row.name->setText(c.name);
                    row.meta->setText(categoryName(c.categoryId));
                } else if (r.type == ContentType::Movie) {
                    const Movie &m = app.vod().movies().items()[(size_t) r.index];
                    row.badgeText->setText("MOVIE");
                    row.badge->setFillColor(Color(120, 72, 150));
                    row.name->setText(m.title);
                    row.meta->setText(join(m.year > 0 ? std::to_string(m.year) : "", fmt::rating(m.rating)));
                } else {
                    const Series &se = app.vod().series().items()[(size_t) r.index];
                    row.badgeText->setText("SERIES");
                    row.badge->setFillColor(Color(40, 128, 120));
                    row.name->setText(se.title);
                    row.meta->setText(join(se.year > 0 ? std::to_string(se.year) : "", se.genre));
                }
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    break;
                case PadButton::Down:
                    list->moveSelection(1);
                    break;
                case PadButton::L2:
                    list->moveSelection(-(list->visibleCount() - 1));
                    break;
                case PadButton::R2:
                    list->moveSelection(list->visibleCount() - 1);
                    break;
                case PadButton::Triangle:
                    if (!e.repeat) {
                        edit();
                    }
                    break;
                case PadButton::Cross:
                    if (!e.repeat && !results.empty()) {
                        open(list->selected());
                    }
                    break;
                case PadButton::Circle:
                    app.pop();
                    break;
                default:
                    break;
            }
        }

    private:
        struct Row {
            RectangleShape *bg;
            RectangleShape *badge;
            ui::Label *badgeText;
            ui::Label *name;
            ui::Label *meta;
        };

        static std::string join(const std::string &a, const std::string &b) {
            return a.empty() ? b : b.empty() ? a : a + "   \xC2\xB7   " + b;
        }

        std::string categoryName(const std::string &id) const {
            for (const auto &c: app.session().live.categories()) {
                if (c.id == id) {
                    return c.name;
                }
            }
            return "";
        }

        void edit() {
            app.push(screens::makeKeyboard(app, "Search channels, movies and series", query, false,
                                           [this](const std::string &q) {
                query = q;
                run();
            }));
        }

        void run() {
            results.clear();
            for (int i: app.session().live.search(query, 150)) {
                results.push_back({ContentType::Live, i});
            }
            for (int i: app.vod().movies().search(query, 200)) {
                results.push_back({ContentType::Movie, i});
            }
            for (int i: app.vod().series().search(query, 150)) {
                results.push_back({ContentType::Series, i});
            }
            list->setSelected(0);
            refresh();
        }

        void open(int index) {
            const Result &r = results[(size_t) index];
            if (r.type == ContentType::Live) {
                // zap context: the live results of this search
                std::vector<int> liveResults;
                int position = 0;
                for (size_t i = 0; i < results.size(); i++) {
                    if (results[i].type == ContentType::Live) {
                        if ((int) i == index) {
                            position = (int) liveResults.size();
                        }
                        liveResults.push_back(results[i].index);
                    }
                }
                app.push(screens::makeLivePlayer(app, liveResults, position));
            } else if (r.type == ContentType::Movie) {
                app.push(screens::makeMovieDetail(app, app.vod().movies().items()[(size_t) r.index]));
            } else {
                app.push(screens::makeSeriesDetail(app, app.vod().series().items()[(size_t) r.index]));
            }
        }

        void refresh() {
            queryText->setText(query.empty() ? "Type to search" : query);
            queryText->setColor(query.empty() ? theme::textMuted() : theme::text());
            summary->setText(query.empty() ? "" : std::to_string(results.size()) + " result"
                                                  + (results.size() == 1 ? "" : "s") + " for \"" + query + "\"");
            if (!app.session().liveLoaded) {
                empty->setText("Nothing to search yet: the channel list is not loaded.");
            } else if (!query.empty() && results.empty()) {
                bool vodPending = app.vod().movieStatus().status == CatalogStatus::Loading
                                  || app.vod().seriesStatus().status == CatalogStatus::Loading;
                empty->setText("Nothing matches \"" + query + "\"."
                               + std::string(vodPending ? " Movies and series are still loading" "\xE2\x80\xA6" : ""));
            } else {
                empty->setText("");
            }
        }

        std::string query;
        unsigned vodGen = 0;
        std::vector<Result> results;
        std::vector<Row> rows;
        ui::ListView *list;
        ui::Label *queryText;
        ui::Label *summary;
        ui::Label *empty;
    };
}

namespace screens {
    Screen *makeSearch(App &app) {
        return new SearchScreen(app);
    }
}
