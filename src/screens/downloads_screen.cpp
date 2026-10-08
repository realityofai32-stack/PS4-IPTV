// Downloads: movies and episodes saved on the console for offline playback.
//
// Two tabs (L1 / R1, or Left / Right): Downloading (queued, active, paused, waiting, failed) and Downloaded.
// Everything comes from the download manifest, so the screen works offline and for downloads of profiles that
// were deleted. Rows are refreshed at most 4 times a second (the manager smooths speed and ETA).
//
// DS4: X plays a downloaded item / opens the actions of a transfer; Square pauses or resumes; Triangle /
// OPTIONS open the actions menu; Circle goes back; holding Up / Down scrolls (InputManager repeat).

#include <algorithm>
#include <cmath>

#include "common.h"
#include "../app/offline.h"
#include "../core/format.h"
#include "../platform/clock.h"
#include "../platform/fs.h"

using namespace c2d;
using namespace iptv;
using screens::VodItem;

namespace {

    const float LIST_Y = 236;
    const float LIST_H = 744;
    const float ROW_H = 132;
    const float POSTER_W = 76;
    const float POSTER_H = 114;
    const double REFRESH_EVERY = 0.25;

    const char *stateLabelKey(dl::State s) {
        switch (s) {
            case dl::State::Queued:
                return "download.state.queued";
            case dl::State::Downloading:
                return "download.state.downloading";
            case dl::State::Paused:
                return "download.state.paused";
            case dl::State::Completed:
                return "download.state.completed";
            case dl::State::Failed:
                return "download.state.failed";
            case dl::State::WaitingForNetwork:
                return "download.state.waiting";
            default:
                return "download.state.cancelled";
        }
    }

    // why a download stopped, in words
    std::string problemText(const dl::Item &d) {
        switch (d.problem) {
            case dl::Problem::Network:
                return tr("download.problem.network");
            case dl::Problem::HttpAuth:
                return tr("download.problem.http_auth");
            case dl::Problem::HttpForbidden:
                return tr("download.problem.http_forbidden");
            case dl::Problem::HttpNotFound:
                return tr("download.problem.http_not_found");
            case dl::Problem::HttpServer:
                return d.httpStatus > 0 ? tr("download.problem.http_status", {std::to_string(d.httpStatus)})
                                        : tr("download.problem.server");
            case dl::Problem::NoSpace:
                return d.neededBytes > 0 && d.availableBytes >= 0
                       ? tr("download.problem.no_space_detail", {dl::formatBytes(d.neededBytes),
                                                                 dl::formatBytes(d.availableBytes)})
                       : tr("download.problem.no_space");
            case dl::Problem::Storage:
                return tr("download.problem.storage");
            case dl::Problem::FileTooLarge:
                return tr("download.problem.too_large");
            case dl::Problem::SizeMismatch:
                return tr("download.problem.size_mismatch");
            case dl::Problem::RestartNeeded:
                return tr("download.problem.restart_needed");
            case dl::Problem::ProfileMissing:
                return tr("download.problem.profile_missing");
            case dl::Problem::FileMissing:
                return tr("download.problem.file_missing");
            default:
                return "";
        }
    }

    std::string titleOf(const dl::Item &d) {
        return d.kind == dl::Kind::Episode && !d.seriesName.empty() ? d.seriesName : d.title;
    }

    std::string subtitleOf(const dl::Item &d) {
        if (d.kind == dl::Kind::Episode) {
            return fmt::episodeCode(d.season, d.episode) + "  \xC2\xB7  "
                   + (d.title.empty() ? tr("episode.number", {std::to_string(d.episode)}) : d.title);
        }
        return d.year > 0 ? std::to_string(d.year) : tr("home.kind_movie");
    }

    class DownloadsScreen : public Screen, public ui::ListView::Adapter {
    public:
        explicit DownloadsScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, tr("downloads.title"));
            storageLine = ui::label(this, "", theme::LABEL, theme::SAFE_X, theme::SAFE_Y + 62, ui::Weight::Regular,
                                    theme::textDim());
            storageLine->setMaxWidth(theme::SCREEN_W - 2 * theme::SAFE_X);
            for (int i = 0; i < 2; i++) {
                tabs[i] = ui::box(this, FloatRect(theme::SAFE_X + (float) i * 340, 160, 320, 56), theme::surface(), 28);
                tabLabels[i] = ui::label(tabs[i], "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, 56),
                                         ui::Weight::SemiBold);
                tabLabels[i]->setAlign(ui::Align::Center, 320);
                tabLabels[i]->setMaxWidth(296);
            }
            list = new ui::ListView(FloatRect(theme::SAFE_X, LIST_Y, theme::SCREEN_W - 2 * theme::SAFE_X, LIST_H),
                                    ROW_H, 10, this);
            add(list);
            empty = ui::label(this, "", theme::BODY, theme::SAFE_X, LIST_Y + 200, ui::Weight::Regular, theme::textMuted());
            empty->setAlign(ui::Align::Center, theme::SCREEN_W - 2 * theme::SAFE_X);
            empty->setMaxWidth(1300);
            empty->setMaxLines(3);
            hints = screens::hintBar(this, {});
            reload(true);
        }

        const char *name() const override { return "downloads"; }

        void onEnter() override {
            requestImages();
        }

        void onResume() override {
            reload(false);
            requestImages();
        }

        void onPause() override {
            app.images().want(std::vector<ImageRequest>());
        }

        void tick(double now) override {
            if (app.images().generation() != imageGen) {
                imageGen = app.images().generation();
                list->reload();
                redraw();
            }
            if (app.downloads().generation() != downloadsGen && now - lastRefresh >= REFRESH_EVERY) {
                lastRefresh = now;
                reload(false);
                redraw();
            }
        }

        // ------------------------------------------------------------------ rows
        int count() override { return (int) shown.size(); }

        C2DObject *createRow(float w, float h) override {
            Row r;
            r.bg = ui::box(nullptr, FloatRect(0, 0, w, h), theme::surface(), theme::RADIUS_SMALL);
            r.poster = new ui::PosterView(FloatRect(18, (h - POSTER_H) / 2, POSTER_W, POSTER_H), theme::CAPTION);
            r.bg->add(r.poster);
            const float tx = 18 + POSTER_W + 28;
            const float right = 620;
            r.title = ui::label(r.bg, "", theme::BODY, tx, 22, ui::Weight::SemiBold);
            r.title->setMaxWidth(w - tx - right - 24);
            r.sub = ui::label(r.bg, "", theme::LABEL, tx, 62, ui::Weight::Regular, theme::textDim());
            r.sub->setMaxWidth(w - tx - right - 24);
            r.extra = ui::label(r.bg, "", theme::CAPTION, tx, 96, ui::Weight::Regular, theme::textMuted());
            r.extra->setMaxWidth(w - tx - right - 24);
            r.state = ui::label(r.bg, "", theme::LABEL, 0, 22, ui::Weight::SemiBold, theme::accent());
            r.state->setAlign(ui::Align::Right, w - 28);
            r.state->setMaxWidth(right - 28);
            r.barTrack = ui::box(r.bg, FloatRect(w - 28 - (right - 40), 64, right - 40, 8), Color(255, 255, 255, 50), 4);
            r.barFill = ui::box(r.barTrack, FloatRect(0, 0, 8, 8), theme::accent(), 4);
            r.detail = ui::label(r.bg, "", theme::CAPTION, 0, 88, ui::Weight::Regular, theme::textDim());
            r.detail->setAlign(ui::Align::Right, w - 28);
            r.detail->setMaxWidth(right - 28);
            rows.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int index, bool, bool focused) override {
            const dl::Item &d = shown[(size_t) index];
            for (auto &r: rows) {
                if (r.bg != obj) {
                    continue;
                }
                r.bg->setFillColor(focused ? theme::rowFocus() : theme::surface());
                r.bg->setOutlineColor(theme::accent());
                r.bg->setOutlineThickness(focused ? 3 : 0);
                std::shared_ptr<ImageSet> img = app.images().get(ImageKind::Poster, d.poster);
                r.poster->set(titleOf(d), img ? img->at(0).texture : nullptr, img ? img->at(0).size : Vector2i());
                r.title->setText(titleOf(d));
                r.sub->setText(subtitleOf(d));
                r.extra->setText(profileNote(d));
                bindState(r, d);
            }
        }

        // ------------------------------------------------------------------ input
        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    break;
                case PadButton::Down:
                    list->moveSelection(1);
                    break;
                case PadButton::L2:
                case PadButton::R2:
                    list->moveSelection((e.button == PadButton::L2 ? -1 : 1) * list->pageSize());
                    break;
                case PadButton::L1:
                case PadButton::R1:
                case PadButton::Left:
                case PadButton::Right:
                    if (!e.repeat) {
                        int t = e.button == PadButton::L1 || e.button == PadButton::Left ? 0 : 1;
                        if (t != tab) {
                            tab = t;
                            list->setSelected(0);
                            reload(false);
                        }
                    }
                    break;
                case PadButton::Cross:
                    if (!e.repeat && selected()) {
                        if (tab == 1) {
                            play(*selected(), true);
                        } else {
                            openActions(*selected());
                        }
                    }
                    return;
                case PadButton::Square:
                    if (!e.repeat && selected() && tab == 0) {
                        togglePause(*selected());
                    }
                    return;
                case PadButton::Triangle:
                case PadButton::Options:
                    if (!e.repeat && selected()) {
                        openActions(*selected());
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
            requestImages();
            refreshHints();
        }

    private:
        struct Row {
            RectangleShape *bg;
            ui::PosterView *poster;
            ui::Label *title;
            ui::Label *sub;
            ui::Label *extra;
            ui::Label *state;
            RectangleShape *barTrack;
            RectangleShape *barFill;
            ui::Label *detail;
        };

        const dl::Item *selected() const {
            int i = list->selected();
            return i >= 0 && i < (int) shown.size() ? &shown[(size_t) i] : nullptr;
        }

        // the downloads of other (or deleted) profiles say whose they are
        std::string profileNote(const dl::Item &d) const {
            if (d.profileId == app.session().profile.id || !multipleProfiles) {
                return "";
            }
            return tr("downloads.profile", {d.profileName.empty() ? d.profileId : d.profileName});
        }

        void bindState(Row &r, const dl::Item &d) {
            bool done = d.state == dl::State::Completed;
            r.barTrack->setVisibility(done ? Visibility::Hidden : Visibility::Visible);
            if (done) {
                const HistoryEntry *p = progressOf(d);
                r.state->setText(p && p->watched ? "\xE2\x9C\x93  " + tr("progress.watched") : "");
                r.state->setColor(theme::success());
                std::string when = d.completedAt > 0 ? clockx::localDate(d.completedAt) : "";
                r.detail->setText(dl::formatBytes(d.downloadedBytes) + (when.empty() ? "" : "  \xC2\xB7  " + when));
                if (p && progress::inProgress(p->position, p->duration, p->watched)) {
                    r.state->setText(fmt::remaining(p->position, p->duration));
                    r.state->setColor(theme::textDim());
                }
                return;
            }
            int64_t bytes = d.downloadedBytes;
            int64_t total = d.expectedBytes;
            double speed = 0;
            double eta = -1;
            if (d.key == liveProgress.key && d.state == dl::State::Downloading) {
                bytes = liveProgress.bytes;
                total = liveProgress.total;
                speed = liveProgress.speed;
                eta = liveProgress.eta;
            }
            int pct = dl::percent(bytes, total);
            std::string state = tr(stateLabelKey(d.state));
            if (d.state == dl::State::Downloading && pct >= 0) {
                state = tr("download.state.downloading_pct", {std::to_string(pct)});
            } else if (d.state == dl::State::Queued && suspended) {
                state = tr("download.state.playback");
            } else if (d.state == dl::State::Queued && d.problem != dl::Problem::None) {
                state = tr("download.state.retrying");
            }
            r.state->setText(state);
            r.state->setColor(d.state == dl::State::Failed ? theme::danger()
                              : d.state == dl::State::Paused || d.state == dl::State::WaitingForNetwork ? theme::warning()
                                                                                                         : theme::accent());
            float w = r.barTrack->getSize().x;
            float frac = pct >= 0 ? (float) pct / 100.0f : 0.0f;
            r.barFill->setSize(std::max(8.0f, std::round(w * frac)), 8);
            r.barFill->setFillColor(d.state == dl::State::Downloading ? theme::accent() : theme::textMuted());
            // "4.2 GB / 7.8 GB · 3.8 MB/s · ~16 min left"; an unknown total shows the bytes only
            std::string detail = total > 0 ? tr("download.size_of", {dl::formatBytes(bytes), dl::formatBytes(total)})
                                           : dl::formatBytes(bytes);
            if (d.state == dl::State::Downloading) {
                if (speed > 0) {
                    detail += "  \xC2\xB7  " + dl::formatBytes((int64_t) speed) + tr("download.per_second");
                }
                if (eta >= 0) {
                    detail += "  \xC2\xB7  " + tr("download.eta", {fmt::duration(std::max(60.0, eta))});
                }
            } else if (d.problem != dl::Problem::None) {
                detail = problemText(d);
            }
            r.detail->setText(detail);
            r.detail->setColor(d.state == dl::State::Failed ? theme::danger() : theme::textDim());
        }

        const HistoryEntry *progressOf(const dl::Item &d) const {
            if (d.profileId != app.library().profileId()) {
                return nullptr;
            }
            return app.library().progressOf(d.kind == dl::Kind::Episode ? ContentType::Series : ContentType::Movie,
                                            d.contentId);
        }

        void reload(bool chooseTab) {
            downloadsGen = app.downloads().generation();
            std::vector<dl::Item> all = app.downloads().items();
            liveProgress = app.downloads().live();
            suspended = app.downloads().playbackActive();
            std::vector<dl::Item> active, done;
            std::string firstProfile;
            multipleProfiles = false;
            for (auto &d: all) {
                if (firstProfile.empty()) {
                    firstProfile = d.profileId;
                }
                multipleProfiles |= d.profileId != firstProfile || d.profileId != app.session().profile.id;
                (d.state == dl::State::Completed ? done : active).push_back(d);
            }
            std::sort(active.begin(), active.end(), [](const dl::Item &a, const dl::Item &b) { return a.order < b.order; });
            std::sort(done.begin(), done.end(), [](const dl::Item &a, const dl::Item &b) {
                return a.completedAt != b.completedAt ? a.completedAt > b.completedAt : a.order > b.order;
            });
            if (chooseTab) {
                tab = active.empty() ? 1 : 0;
            }
            std::string keep = selected() ? selected()->key : "";
            shown = tab == 0 ? active : done;
            int sel = 0;
            for (size_t i = 0; i < shown.size(); i++) {
                if (shown[i].key == keep) {
                    sel = (int) i;
                }
            }
            list->setSelected(std::min(sel, std::max(0, (int) shown.size() - 1)));
            list->reload();

            dl::Totals t = app.downloads().totals();
            int64_t free = app.downloads().freeBytes();
            std::string line = tr("downloads.storage", {dl::formatBytes(t.completedBytes),
                                                        free >= 0 ? dl::formatBytes(free) : tr("common.unknown")});
            if (suspended) {
                line += "   \xC2\xB7   " + tr("downloads.paused_for_playback");
            }
            storageLine->setText(line);
            int counts[2] = {(int) active.size(), (int) done.size()};
            const char *names[2] = {"downloads.tab_downloading", "downloads.tab_downloaded"};
            for (int i = 0; i < 2; i++) {
                tabLabels[i]->setText(tr(names[i], {std::to_string(counts[i])}));
                bool f = i == tab;
                tabs[i]->setFillColor(f ? theme::accentDark() : theme::surface());
                tabLabels[i]->setColor(f ? Color::White : theme::textDim());
            }
            empty->setText(tr(tab == 0 ? "downloads.empty_active" : "downloads.empty_done"));
            empty->setVisibility(shown.empty() ? Visibility::Visible : Visibility::Hidden);
            refreshHints();
        }

        void refreshHints() {
            const dl::Item *d = selected();
            screens::Hints h;
            if (d && tab == 1) {
                h.push_back({ui::Glyph::Cross, tr("common.play")});
            } else if (d) {
                h.push_back({ui::Glyph::Cross, tr("downloads.actions")});
                bool running = d->state == dl::State::Downloading || d->state == dl::State::Queued
                               || d->state == dl::State::WaitingForNetwork;
                h.push_back({ui::Glyph::Square, tr(running ? "download.pause" : "download.resume")});
            }
            if (d) {
                h.push_back({ui::Glyph::Triangle, tr("downloads.options")});
            }
            h.push_back({ui::Glyph::L1, ""});
            h.push_back({ui::Glyph::R1, tr("downloads.switch_tab")});
            h.push_back({ui::Glyph::Circle, tr("common.back")});
            hints->setHints(h);
        }

        void requestImages() {
            if (!app.settings().get().loadImages) {
                return;
            }
            std::vector<ImageRequest> req;
            int first = list->firstVisible();
            for (int i = first; i < first + list->visibleCount() + 2 && i < (int) shown.size(); i++) {
                req.push_back({ImageKind::Poster, shown[(size_t) i].poster});
            }
            app.images().want(req);
        }

        void togglePause(const dl::Item &d) {
            if (d.state == dl::State::Downloading || d.state == dl::State::Queued
                || d.state == dl::State::WaitingForNetwork) {
                app.downloads().pause(d.key);
            } else if (d.problem == dl::Problem::RestartNeeded) {
                confirmRestart(d);
            } else {
                app.downloads().resume(d.key);
            }
            reload(false);
        }

        void confirmRestart(const dl::Item &d) {
            std::string key = d.key;
            app.push(screens::makeDialog(app, tr("download.restart_title"),
                                         tr("download.restart_text", {dl::formatBytes(d.downloadedBytes)}),
                                         {tr("common.cancel"), tr("download.restart")}, [this, key](int c) {
                        if (c == 1) {
                            app.downloads().confirmRestart(key);
                        }
                    }));
        }

        void confirmDelete(const dl::Item &d) {
            std::string key = d.key;
            bool done = d.state == dl::State::Completed;
            app.push(screens::makeDialog(app, tr(done ? "download.delete_title" : "download.cancel_title"),
                                         tr(done ? "download.delete_text" : "download.cancel_text", {titleOf(d)}),
                                         {tr("common.cancel"), tr(done ? "download.delete" : "download.cancel")},
                                         guardedChoice([this, key, done](int c) {
                                             if (c != 1) {
                                                 return;
                                             }
                                             if (done) {
                                                 std::string err;
                                                 if (app.downloads().remove(key, &err)) {
                                                     app.toast(tr("download.deleted"), ToastKind::Success);
                                                 } else {
                                                     app.toast(tr("download.delete_failed"), ToastKind::Error);
                                                 }
                                             } else {
                                                 app.downloads().cancel(key);
                                                 app.toast(tr("download.cancelled"));
                                             }
                                             reload(false);
                                         }), true));
        }

        std::function<void(int)> guardedChoice(std::function<void(int)> f) {
            std::weak_ptr<bool> w = token;
            return [w, f](int c) {
                if (w.lock()) {
                    f(c);
                }
            };
        }

        void openActions(const dl::Item &d) {
            std::vector<std::string> names;
            std::vector<std::function<void()>> actions;
            dl::Item item = d;
            if (d.state == dl::State::Completed) {
                const HistoryEntry *p = progressOf(d);
                bool resumable = p && progress::inProgress(p->position, p->duration, p->watched);
                names.push_back(resumable ? tr("download.resume_offline", {fmt::clock(p->position)})
                                          : tr("download.play_offline"));
                actions.push_back([this, item] { play(item, true); });
                if (resumable) {
                    names.push_back(tr("common.start_over"));
                    actions.push_back([this, item] { play(item, false); });
                }
                names.push_back(tr("download.delete"));
                actions.push_back([this, item] { confirmDelete(item); });
            } else {
                if (d.problem == dl::Problem::RestartNeeded) {
                    names.push_back(tr("download.restart"));
                    actions.push_back([this, item] { confirmRestart(item); });
                } else if (d.state == dl::State::Downloading || d.state == dl::State::Queued
                           || d.state == dl::State::WaitingForNetwork) {
                    names.push_back(tr("download.pause"));
                    actions.push_back([this, item] {
                        app.downloads().pause(item.key);
                        reload(false);
                    });
                } else {
                    names.push_back(tr(d.state == dl::State::Failed ? "download.retry" : "download.resume"));
                    actions.push_back([this, item] {
                        app.downloads().resume(item.key);
                        reload(false);
                    });
                }
                names.push_back(tr("download.cancel"));
                actions.push_back([this, item] { confirmDelete(item); });
            }
            std::weak_ptr<bool> w = token;
            app.push(screens::makeMenu(app, titleOf(d), names, -1, [w, actions](int i) {
                if (w.lock() && i >= 0 && i < (int) actions.size()) {
                    actions[(size_t) i]();
                }
            }));
        }

        // plays a completed download through the normal player (local file instead of the stream)
        void play(const dl::Item &d, bool resume) {
            if (d.state != dl::State::Completed) {
                return;
            }
            if (fs::fileSize(app.downloads().completedPath(d)) <= 0) {
                app.toast(tr("download.problem.file_missing"), ToastKind::Error);
                return;
            }
            std::vector<VodItem> queue;
            int position = 0;
            if (d.kind == dl::Kind::Episode) {
                for (const dl::Item &e: offline::seriesEpisodes(app.downloads(), d.profileId, d.seriesId)) {
                    if (e.key == d.key) {
                        position = (int) queue.size();
                    }
                    queue.push_back(offline::itemFor(app.downloads(), e));
                }
            }
            if (queue.empty()) {
                queue.push_back(offline::itemFor(app.downloads(), d));
            }
            if (!resume && d.profileId == app.library().profileId()) {
                // start over: like Start over in the movie details (the position is forgotten, the history kept)
                app.library().resetProgress(d.kind == dl::Kind::Episode ? ContentType::Series : ContentType::Movie,
                                            d.contentId);
            }
            app.push(screens::makeVodPlayer(app, queue, position, resume));
        }

        ui::Label *storageLine;
        RectangleShape *tabs[2];
        ui::Label *tabLabels[2];
        ui::ListView *list;
        ui::Label *empty;
        ui::HintBar *hints;
        std::vector<Row> rows;
        std::vector<dl::Item> shown;
        dl::LiveProgress liveProgress;
        std::shared_ptr<bool> token = std::make_shared<bool>(true);
        bool suspended = false;
        bool multipleProfiles = false;
        int tab = 0;
        unsigned downloadsGen = 0;
        unsigned imageGen = 0;
        double lastRefresh = 0;
    };
}

namespace screens {
    Screen *makeDownloads(App &app) {
        return new DownloadsScreen(app);
    }
}
