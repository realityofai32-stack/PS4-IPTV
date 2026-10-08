// Favorites of all types: Channels | Movies | Series tabs (L1/R1). Movies and Series catalogs are loaded
// lazily (saved copy first) when the screen opens, so their favorites can be shown with posters.
// Favorites are per source; playlist (M3U) sources only have the Channels tab.

#include "common.h"
#include "../core/format.h"

using namespace c2d;
using namespace iptv;

namespace {

    const float TOP = 236;
    const float CONTENT_H = 740;
    const char *TAB_KEYS[] = {"favorites.tab_channels", "home.movies", "home.series"};

    class FavoritesScreen : public Screen {
    public:
        explicit FavoritesScreen(App &a) : Screen(a), listAdapter(this), gridAdapter(this) {
            tabs = app.session().profile.isPlaylist() ? 1 : 3;
            liveGen = app.session().liveGeneration;
            ui::background(this);
            screens::header(this, tr("home.favorites"));
            tabLayer = new RectangleShape(FloatRect(theme::SAFE_X, 140, 1200, 60));
            tabLayer->setFillColor(theme::none());
            add(tabLayer);
            channels = new ui::ListView(FloatRect(theme::SAFE_X, TOP, theme::SCREEN_W - 2 * theme::SAFE_X, CONTENT_H),
                                        84, 8, &listAdapter);
            add(channels);
            grid = new ui::GridView(FloatRect(theme::SAFE_X, TOP, theme::SCREEN_W - 2 * theme::SAFE_X, CONTENT_H),
                                    140, 246, 10, 3, &gridAdapter);
            add(grid);
            empty = ui::label(this, "", theme::BODY, 0, TOP + 280, ui::Weight::Regular, theme::textMuted());
            empty->setAlign(ui::Align::Center, theme::SCREEN_W);
            empty->setMaxWidth(1200);
            empty->setMaxLines(2);
            screens::hintBar(this, {{ui::Glyph::Cross, tr("common.open")}, {ui::Glyph::L1, ""}, {ui::Glyph::R1, tr("favorites.type")},
                                    {ui::Glyph::Square, tr("common.remove")}, {ui::Glyph::Circle, tr("common.back")}});
            app.vod().openMovies(app.session().profile);
            app.vod().openSeries(app.session().profile);
            rebuild();
        }

        const char *name() const override { return "favorites"; }

        void onResume() override {
            liveGen = app.session().liveGeneration;
            rebuild();
        }

        void onPause() override {
            app.images().want(std::vector<ImageRequest>());
        }

        void tick(double) override {
            if (app.vod().generation() != vodGen || app.images().generation() != imageGen
                || app.session().liveGeneration != liveGen) {
                liveGen = app.session().liveGeneration;
                imageGen = app.images().generation();
                rebuild();
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::L1:
                case PadButton::R1: {
                    int t = tab + (e.button == PadButton::L1 ? -1 : 1);
                    if (!e.repeat && t >= 0 && t < tabs) {
                        tab = t;
                        rebuild();
                    }
                    return;
                }
                case PadButton::Up:
                case PadButton::Down:
                case PadButton::Left:
                case PadButton::Right: {
                    int dx = e.button == PadButton::Left ? -1 : e.button == PadButton::Right ? 1 : 0;
                    int dy = e.button == PadButton::Up ? -1 : e.button == PadButton::Down ? 1 : 0;
                    if (tab == 0) {
                        if (dy != 0) {
                            channels->moveSelection(dy);
                        } else if (dx > 0 && !e.repeat && tabs > 1) {
                            tab = 1;
                            rebuild();
                        }
                    } else if (!grid->navigate(dx, dy) && dx != 0 && !e.repeat) {
                        int t = tab + dx;
                        if (t >= 0 && t < tabs) {
                            tab = t;
                            rebuild();
                        }
                    }
                    requestImages();
                    return;
                }
                case PadButton::L2:
                case PadButton::R2: {
                    int d = e.button == PadButton::L2 ? -1 : 1;
                    if (tab == 0) {
                        channels->moveSelection(d * channels->pageSize());
                    } else {
                        grid->page(d);
                    }
                    requestImages();
                    return;
                }
                case PadButton::Cross:
                    if (!e.repeat) {
                        open();
                    }
                    return;
                case PadButton::Square:
                    if (!e.repeat) {
                        removeFavorite();
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
        }

        // ------------------------------------------------------------------ channel rows
        int count() const { return tab == 0 ? (int) liveItems.size() : (int) vodItems.size(); }

        C2DObject *createRow(float w, float h) {
            ChannelRow r;
            r.bg = ui::box(nullptr, FloatRect(0, 0, w, h), theme::surface(), theme::RADIUS_SMALL);
            r.logo = new ui::LogoView(FloatRect(16, (h - 60) / 2, 104, 60), 22, 4, 1.25f);
            r.bg->add(r.logo);
            r.name = ui::label(r.bg, "", theme::BODY, 144, ui::Label::centerOffset(theme::BODY, h));
            r.name->setMaxWidth(w - 144 - 500);
            r.meta = ui::label(r.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h), ui::Weight::Regular,
                               theme::textSecondary());
            r.meta->setAlign(ui::Align::Right, w - 24);
            r.meta->setMaxWidth(440);
            rows.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int index, bool focused) {
            const LiveChannel &c = app.session().live.channels()[(size_t) liveItems[(size_t) index]];
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                std::shared_ptr<ImageSet> img = app.images().get(c.icon);
                r.logo->set(c.name, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                r.name->setText(c.name);
                r.meta->setText(categoryName(c.categoryId));
                r.bg->setFillColor(focused ? theme::focus() : theme::surface());
                r.bg->setOutlineColor(theme::accent());
                r.bg->setOutlineThickness(focused ? 3 : 0);
            }
        }

        // ------------------------------------------------------------------ poster cells
        C2DObject *createCell(float w, float h) {
            Cell c;
            c.root = new RectangleShape(FloatRect(0, 0, w, h));
            c.root->setFillColor(theme::none());
            c.poster = new ui::PosterView(FloatRect(0, 0, w, 210), theme::CAPTION);
            c.root->add(c.poster);
            c.title = ui::label(c.root, "", theme::CAPTION, 0, 218, ui::Weight::Regular, theme::textSecondary());
            c.title->setAlign(ui::Align::Center, w);
            c.title->setMaxWidth(w);
            cells.push_back(c);
            return c.root;
        }

        void bindCell(C2DObject *obj, int index, bool focused) {
            std::string title, image;
            itemInfo(index, title, image);
            for (auto &c: cells) {
                if (c.root != obj) {
                    continue;
                }
                std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Poster, image);
                c.poster->set(title, img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                c.poster->setProgress(0);
                c.poster->setWatched(false);
                c.poster->setFocused(focused);
                c.title->setText(title);
                c.title->setColor(focused ? theme::textStrong() : theme::textSecondary());
            }
        }

    private:
        struct ListAdapter : ui::ListView::Adapter {
            explicit ListAdapter(FavoritesScreen *s) : screen(s) {}

            int count() override { return (int) screen->liveItems.size(); }

            C2DObject *createRow(float w, float h) override { return screen->createRow(w, h); }

            void bindRow(C2DObject *row, int index, bool, bool focused) override {
                screen->bindRow(row, index, focused);
            }

            FavoritesScreen *screen;
        };

        struct GridAdapter : ui::GridView::Adapter {
            explicit GridAdapter(FavoritesScreen *s) : screen(s) {}

            int count() override { return (int) screen->vodItems.size(); }

            C2DObject *createCell(float w, float h) override { return screen->createCell(w, h); }

            void bindCell(C2DObject *cell, int index, bool focused) override { screen->bindCell(cell, index, focused); }

            FavoritesScreen *screen;
        };

        struct ChannelRow {
            RectangleShape *bg;
            ui::LogoView *logo;
            ui::Label *name;
            ui::Label *meta;
        };

        struct Cell {
            RectangleShape *root;
            ui::PosterView *poster;
            ui::Label *title;
        };

        std::string categoryName(const std::string &id) const {
            return app.session().live.categoryName(id);   // Uncategorized in the current UI language
        }

        void itemInfo(int index, std::string &title, std::string &image) const {
            int i = vodItems[(size_t) index];
            if (tab == 1) {
                const Movie &m = app.vod().movies().items()[(size_t) i];
                title = m.title;
                image = m.icon;
            } else {
                const Series &s = app.vod().series().items()[(size_t) i];
                title = s.title;
                image = s.cover;
            }
        }

        void rebuild() {
            vodGen = app.vod().generation();
            liveItems = app.session().live.favorites(app.library().favorites(ContentType::Live));
            if (tab == 1) {
                vodItems = app.vod().movies().favorites(app.library().favorites(ContentType::Movie));
            } else if (tab == 2) {
                vodItems = app.vod().series().favorites(app.library().favorites(ContentType::Series));
            } else {
                vodItems.clear();
            }
            size_t counts[3] = {liveItems.size(), app.library().favorites(ContentType::Movie).size(),
                                app.library().favorites(ContentType::Series).size()};
            for (auto *c: tabLayer->getChilds()) {
                tabLayer->remove(c);
                delete c;
            }
            float x = 0;
            for (int t = 0; t < tabs; t++) {
                bool sel = t == tab;
                std::string text = tr(TAB_KEYS[t]) + "  " + std::to_string(counts[t]);
                auto *chip = ui::box(tabLayer, FloatRect(x, 0, 260, 56), sel ? theme::accentMuted() : theme::surface(), 28);
                auto *l = ui::label(chip, text, theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, 56),
                                    sel ? ui::Weight::SemiBold : ui::Weight::Regular, sel ? theme::textStrong() : theme::textSecondary());
                l->setAlign(ui::Align::Center, 260);
                x += 276;
            }
            channels->setVisibility(tab == 0 ? Visibility::Visible : Visibility::Hidden);
            grid->setVisibility(tab != 0 ? Visibility::Visible : Visibility::Hidden);
            channels->reload();
            grid->reload();
            const SectionStatus &st = tab == 1 ? app.vod().movieStatus() : app.vod().seriesStatus();
            bool loading = tab != 0 && counts[tab] > 0 && vodItems.empty()
                           && (st.status == CatalogStatus::Loading || st.status == CatalogStatus::NotLoaded);
            if (loading) {
                empty->setText(tr("common.loading"));
            } else if (count() == 0) {
                empty->setText(tr(tab == 0 ? "favorites.empty_channels" : tab == 1 ? "favorites.empty_movies"
                                                                                : "favorites.empty_series"));
            } else {
                empty->setText("");
            }
            requestImages();
        }

        void requestImages() {
            if (!app.settings().get().loadImages) {
                return;
            }
            std::vector<ImageRequest> req;
            if (tab == 0) {
                int first = channels->firstVisible();
                for (int i = first; i < first + channels->visibleCount() && i < (int) liveItems.size(); i++) {
                    req.push_back({ImageKind::Logo, app.session().live.channels()[(size_t) liveItems[(size_t) i]].icon});
                }
            } else {
                int first = grid->firstVisible();
                for (int i = first; i < first + grid->cellCount() && i < (int) vodItems.size(); i++) {
                    std::string title, image;
                    itemInfo(i, title, image);
                    req.push_back({ImageKind::Poster, image});
                }
            }
            app.images().want(req);
        }

        void open() {
            if (tab == 0) {
                if (!liveItems.empty()) {
                    app.push(screens::makeLivePlayer(app, liveItems, channels->selected()));
                }
            } else if (!vodItems.empty()) {
                int i = vodItems[(size_t) grid->selected()];
                if (tab == 1) {
                    app.push(screens::makeMovieDetail(app, app.vod().movies().items()[(size_t) i]));
                } else {
                    app.push(screens::makeSeriesDetail(app, app.vod().series().items()[(size_t) i]));
                }
            }
        }

        void removeFavorite() {
            if (count() == 0) {
                return;
            }
            if (tab == 0) {
                app.library().toggleFavorite(ContentType::Live,
                                             app.session().live.channels()[(size_t) liveItems[(size_t) channels->selected()]].id);
            } else {
                int i = vodItems[(size_t) grid->selected()];
                if (tab == 1) {
                    app.library().toggleFavorite(ContentType::Movie, app.vod().movies().items()[(size_t) i].streamId);
                } else {
                    app.library().toggleFavorite(ContentType::Series, app.vod().series().items()[(size_t) i].seriesId);
                }
            }
            app.saveLibrary();
            app.toast(tr("favorites.removed"));
            rebuild();
        }

        ListAdapter listAdapter;
        GridAdapter gridAdapter;
        RectangleShape *tabLayer;
        ui::ListView *channels;
        ui::GridView *grid;
        ui::Label *empty;
        std::vector<ChannelRow> rows;
        std::vector<Cell> cells;
        std::vector<int> liveItems;
        std::vector<int> vodItems;
        int tab = 0;
        int tabs = 3;            // Channels, Movies, Series (playlist sources: Channels only)
        unsigned liveGen = 0;
        unsigned vodGen = 0;
        unsigned imageGen = 0;
    };
}

namespace screens {
    Screen *makeFavorites(App &app) {
        return new FavoritesScreen(app);
    }
}
