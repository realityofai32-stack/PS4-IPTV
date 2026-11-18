// My Playlists (media/user_playlists.h): the user's own M3U playlists in /data/PS4IPTV/playlists/.
//
// The list, in the user's order - the order of the playlists' groups in Live TV (app/user_groups.h): Cross opens a
// playlist, Square picks it up (Up / Down carry it, Cross / Square put it down, Circle puts it back), OPTIONS has New
// playlist / Import from USB and, for the selected playlist, Show in / Hide from Live TV, Rename, Open as a source,
// Export to USB, Delete. A file too large to edit here (a provider's whole playlist copied over FTP) is listed and can
// still be opened as a source; it is never a Live TV group.
//
// A playlist: Cross on an entry opens Move / Rename / Remove; Square picks the entry up - Up / Down (L2 / R2) carry
// it, Cross or Square puts it down, Circle puts it back where it was. OPTIONS: Add stream address, Rename, Open as a
// source, Export to USB, Delete. Every change is written at once (fs::writeFileAtomic); a source that opens the file
// loses its saved copy, so it reads the new file (the open one reloads in the background).
//
// Channels are added from Live TV (OPTIONS on a channel: screens::addToMyPlaylist). Playback goes through "Open as a
// source": the playlist becomes (or already is) a playlist source, with Live TV, the live player and its channel
// list - nothing is played from here.
//
// USB: import lists the .m3u / .m3u8 files at the top of each drive (localmedia: no symbolic links, nothing outside
// the drive); export writes <name>.m3u at the top of the chosen drive and never replaces a file there.

#include "common.h"
#include "../core/format.h"
#include "../iptv/m3u.h"
#include "../media/local_media.h"
#include "../app/user_groups.h"
#include "../media/user_playlists.h"
#include "../platform/clock.h"
#include "../platform/fs.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"

using namespace c2d;

namespace {

    namespace L = layout::downloads;

    const float ROW_H = 96;

    std::string playlistsDir() {
        return m3u::LOCAL_PLAYLIST_DIR;
    }

    std::string entriesText(int n) {
        return i18n::count("playlists.entries", n);
    }

    // the profiles that open this file as their source: their saved copy goes (the next opening reads the file); the
    // open one reloads now
    void playlistChanged(App &app, const std::string &path) {
        app.applyUserGroups();   // its Live TV group shows the change
        for (const auto &p: app.profiles().profiles()) {
            if (!p.isPlaylist() || m3u::localPlaylistPath(p.playlistUrl) != path) {
                continue;
            }
            CatalogCache(APP_DATA_DIR).remove(p.id, M3uService::CACHE_NAME);
            if ((app.session().connected || app.session().offline) && app.session().profile.id == p.id) {
                app.refreshPlaylist(false);
            }
        }
    }

    bool savePlaylist(App &app, const userpl::Playlist &p) {
        std::string err;
        if (!userpl::save(p, err)) {
            LOG_W("playlists", "playlist not saved: %s", err.c_str());
            app.toast(tr("playlists.save_failed"), ToastKind::Error);
            return false;
        }
        playlistChanged(app, p.path);
        return true;
    }

    std::vector<localmedia::Root> usbRoots() {
        std::vector<localmedia::Root> out;
        for (const auto &r: localmedia::detectRoots()) {
            if (r.usb) {
                out.push_back(r);
            }
        }
        return out;
    }

    std::string driveName(const localmedia::Root &r) {
        return tr("localmedia.usb", {std::to_string(r.number)});
    }

    // the playlist file becomes the active source (an existing playlist source that opens it, else a new one)
    void openAsSource(App &app, const std::string &path, const std::string &title) {
        std::string id;
        for (const auto &p: app.profiles().profiles()) {
            if (p.isPlaylist() && m3u::localPlaylistPath(p.playlistUrl) == path) {
                id = p.id;
                break;
            }
        }
        int64_t now = clockx::unixNow();
        if (id.empty()) {
            iptv::Profile p;
            p.type = iptv::SourceType::M3u;
            p.name = title.empty() ? tr("playlists.title") : title;
            p.playlistUrl = path;
            id = app.profiles().upsert(p, now);
        }
        app.profiles().setActive(id);
        std::string err;
        if (!app.profiles().save(&err)) {
            LOG_E("profiles", "save failed: %s", err.c_str());
            app.toast(tr("profile.save_failed"), ToastKind::Error);
            return;
        }
        CatalogCache(APP_DATA_DIR).remove(id, M3uService::CACHE_NAME);   // read the file as it is now
        LOG_I("playlists", "opening my playlist as source %s", id.c_str());
        app.replaceAll(screens::makeConnect(app, *app.profiles().find(id)));
    }

    void writeExport(App &app, const userpl::Playlist &p, const localmedia::Root &drive) {
        std::string base = userpl::slug(p.title);
        std::string target = fs::join(drive.path, base + ".m3u");
        for (int n = 2; fs::exists(target) && n < 1000; n++) {
            target = fs::join(drive.path, base + "-" + std::to_string(n) + ".m3u");   // never replaces a file
        }
        std::string err;
        if (!localmedia::within(drive, target) || !fs::writeFileReplace(target, userpl::toM3u(p), &err)) {
            LOG_W("playlists", "export to USB %d failed: %s", drive.number, err.c_str());
            app.toast(tr("playlists.export_failed"), ToastKind::Error);
            return;
        }
        LOG_I("playlists", "exported %d entries to USB %d", (int) p.entries.size(), drive.number);
        app.toast(tr("playlists.exported", {driveName(drive), target.substr(target.find_last_of('/') + 1)}),
                  ToastKind::Success);
    }

    // Export to USB: a warning about the addresses first, then the drive (when there are several)
    void exportPlaylist(App &app, const userpl::Playlist &p) {
        std::vector<localmedia::Root> drives = usbRoots();
        if (drives.empty()) {
            app.toast(tr("playlists.no_usb"), ToastKind::Error);
            return;
        }
        app.push(screens::makeDialog(app, tr("playlists.export"), tr("playlists.export_text"),
                                     {tr("common.cancel"), tr("playlists.export")}, [&app, p, drives](int c) {
            if (c != 1) {
                return;
            }
            if (drives.size() == 1) {
                writeExport(app, p, drives[0]);
                return;
            }
            std::vector<std::string> names;
            for (const auto &d: drives) {
                names.push_back(driveName(d));
            }
            app.push(screens::makeMenu(app, tr("playlists.choose_drive"), names, -1, [&app, p, drives](int d) {
                if (d >= 0 && d < (int) drives.size()) {
                    writeExport(app, p, drives[(size_t) d]);
                }
            }));
        }));
    }

    // keyboard: the name of a new playlist, then onCreated(path)
    void newPlaylist(App &app, std::function<void(const userpl::Playlist &)> onCreated) {
        app.push(screens::makeKeyboard(app, tr("playlists.new_name"), "", false, [&app, onCreated](const std::string &v) {
            std::string title = userpl::cleanName(v);
            if (title.empty()) {
                return;
            }
            userpl::Playlist p;
            p.title = title;
            p.path = userpl::newPath(playlistsDir(), title);
            if (savePlaylist(app, p)) {
                LOG_I("playlists", "new playlist created");
                onCreated(p);
            }
        }));
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class PlaylistEditorScreen : public Screen, public ui::ListView::Adapter {
    public:
        PlaylistEditorScreen(App &a, const std::string &path) : Screen(a) {
            std::string err;
            if (!userpl::load(path, pl, err)) {
                LOG_W("playlists", "playlist not readable: %s", err.c_str());
                pl.path.clear();
            }
            ui::background(this);
            title = screens::header(this, pl.title.empty() ? tr("playlists.title") : pl.title);
            context = new screens::HeaderContext(this, title);
            list = new ui::ListView(FloatRect(theme::SAFE_X, L::LIST_Y, theme::CONTENT_W, L::LIST_H), ROW_H, 4, this);
            add(list);
            empty = new ui::EmptyState(FloatRect(theme::SAFE_X, L::LIST_Y, theme::CONTENT_W, 420));
            add(empty);
            hints = screens::hintBar(this, {});
            refreshAll(0);
        }

        const char *name() const override { return "my-playlist"; }

        int count() override { return (int) pl.entries.size(); }

        C2DObject *createRow(float w, float h) override {
            Row r;
            r.bg = new ui::RowFrame(w, h, 0);
            r.number = ui::label(r.bg, "", theme::LABEL, 28, ui::Label::centerOffset(theme::LABEL, h), ui::Weight::SemiBold,
                                 theme::accentText());
            r.name = ui::label(r.bg, "", theme::BODY, 110, ui::Label::centerOffset(theme::BODY, h));
            r.name->setMaxWidth(w - 110 - 520);
            r.meta = ui::label(r.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h), ui::Weight::Regular,
                               theme::textSecondary());
            r.meta->setAlign(ui::Align::Right, w - 28);
            r.meta->setMaxWidth(480);
            views.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int i, bool, bool focused) override {
            for (auto &v: views) {
                if (v.bg != obj) {
                    continue;
                }
                const userpl::Entry &e = pl.entries[(size_t) i];
                bool held = moving && focused;
                v.number->setText(held ? "\xE2\x86\x95" : std::to_string(i + 1));   // ↕ on the entry being moved
                v.name->setText(e.name);
                v.name->setColor(focused ? theme::textStrong() : theme::textPrimary());
                v.name->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                v.meta->setText(m3u::displayUrl(e.url));
                v.bg->setState(focused, held);
            }
        }

        void handleInput(const InputEvent &e) override {
            if (pl.path.empty()) {
                if (e.button == PadButton::Circle && !e.repeat) {
                    app.pop();
                }
                return;
            }
            int sel = list->selected();
            int page = list->pageSize();
            int step = e.button == PadButton::Up ? -1 : e.button == PadButton::Down ? 1
                       : e.button == PadButton::L2 ? -page : e.button == PadButton::R2 ? page : 0;
            if (moving) {
                if (step != 0) {
                    int to = userpl::move(pl, sel, step);
                    if (to >= 0 && to != sel) {
                        list->setSelected(to);
                        list->reload();
                    }
                } else if ((e.button == PadButton::Cross || e.button == PadButton::Square) && !e.repeat) {
                    moving = false;
                    if (sel != movedFrom) {
                        savePlaylist(app, pl);
                    }
                    refreshAll(sel);
                } else if (e.button == PadButton::Circle && !e.repeat) {
                    userpl::move(pl, sel, movedFrom - sel);   // back where it was
                    moving = false;
                    refreshAll(movedFrom);
                }
                return;
            }
            if (step != 0) {
                list->moveSelection(step);
                return;
            }
            if (e.repeat) {
                return;
            }
            switch (e.button) {
                case PadButton::Cross:
                    if (!pl.entries.empty()) {
                        entryMenu(sel);
                    }
                    return;
                case PadButton::Square:
                    if (pl.entries.size() > 1) {
                        moving = true;
                        movedFrom = sel;
                        refreshAll(sel);
                    }
                    return;
                case PadButton::Options:
                    playlistMenu();
                    return;
                case PadButton::Circle:
                    app.pop();
                    return;
                default:
                    return;
            }
        }

    private:
        struct Row {
            ui::RowFrame *bg;
            ui::Label *number;
            ui::Label *name;
            ui::Label *meta;
        };

        void refreshAll(int select) {
            title->setText(pl.title.empty() ? tr("playlists.title") : pl.title);
            context->set("", pl.path.empty() ? "" : entriesText((int) pl.entries.size()));
            if (pl.path.empty()) {
                empty->set(tr("playlists.unreadable"), tr("playlists.unreadable_text",
                                                          {fmt::number((long long) userpl::MAX_ENTRIES)}));
            } else if (pl.entries.empty()) {
                empty->set(tr("playlists.list_empty"), tr("playlists.list_empty_text", {screens::keyName(A::Options)}));
            } else {
                empty->set("", "");
            }
            list->setSelected(std::min(std::max(select, 0), std::max(0, (int) pl.entries.size() - 1)));
            list->reload();
            if (pl.path.empty()) {
                hints->setHints({{A::Back, tr("common.back")}});
            } else if (moving) {
                hints->setHints({{A::Navigate, tr("playlists.move")}, {A::Confirm, tr("playlists.drop")},
                                 {A::Back, tr("common.cancel")}});
            } else {
                screens::Hints h;
                if (!pl.entries.empty()) {
                    h.push_back({A::Confirm, tr("common.edit")});
                }
                if (pl.entries.size() > 1) {
                    h.push_back({A::Edit, tr("playlists.move")});
                }
                h.push_back({A::Options, tr("live.options")});
                h.push_back({A::Back, tr("common.back")});
                hints->setHints(h);
            }
        }

        void entryMenu(int i) {
            std::vector<std::string> names = {tr("playlists.move"), tr("playlists.rename"), tr("playlists.remove")};
            app.push(screens::makeMenu(app, pl.entries[(size_t) i].name, names, -1, whileAlive([this, i](int c) {
                if (i >= (int) pl.entries.size()) {
                    return;
                }
                if (c == 0 && pl.entries.size() > 1) {
                    moving = true;
                    movedFrom = i;
                    refreshAll(i);
                } else if (c == 1) {
                    app.push(screens::makeKeyboard(app, tr("playlists.entry_name"), pl.entries[(size_t) i].name, false,
                                                   whileAliveText([this, i](const std::string &v) {
                        std::string n = userpl::cleanName(v);
                        if (!n.empty() && i < (int) pl.entries.size()) {
                            pl.entries[(size_t) i].name = n;
                            savePlaylist(app, pl);
                            refreshAll(i);
                        }
                    })));
                } else if (c == 2) {
                    pl.entries.erase(pl.entries.begin() + i);
                    savePlaylist(app, pl);
                    refreshAll(i);
                }
            })));
        }

        void playlistMenu() {
            std::vector<std::string> names = {tr("playlists.add_url"), tr("playlists.rename"), tr("playlists.use_source"),
                                              tr("playlists.export"), tr("playlists.delete")};
            app.push(screens::makeMenu(app, pl.title, names, -1, whileAlive([this](int c) {
                switch (c) {
                    case 0:
                        addAddress();
                        return;
                    case 1:
                        app.push(screens::makeKeyboard(app, tr("playlists.new_name"), pl.title, false,
                                                       whileAliveText([this](const std::string &v) {
                            std::string n = userpl::cleanName(v);
                            if (!n.empty()) {
                                pl.title = n;
                                savePlaylist(app, pl);
                                refreshAll(list->selected());
                            }
                        })));
                        return;
                    case 2:
                        openAsSource(app, pl.path, pl.title);
                        return;
                    case 3:
                        exportPlaylist(app, pl);
                        return;
                    case 4:
                        confirmDelete();
                        return;
                    default:
                        return;
                }
            })));
        }

        // a name, then the address
        void addAddress() {
            if (pl.entries.size() >= userpl::MAX_ENTRIES) {
                app.toast(tr("playlists.full", {pl.title, fmt::number((long long) userpl::MAX_ENTRIES)}), ToastKind::Error);
                return;
            }
            app.push(screens::makeKeyboard(app, tr("playlists.entry_name"), "", false,
                                           whileAliveText([this](const std::string &v) {
                std::string n = userpl::cleanName(v);
                if (n.empty()) {
                    return;
                }
                const char *prefill = "http://";   // i18n-exempt: technical value
                app.push(screens::makeKeyboard(app, tr("playlists.entry_url"), prefill, false,
                                               whileAliveText([this, n](const std::string &u) {
                    std::string url(u);
                    while (!url.empty() && (url.back() == ' ' || url.front() == ' ')) {
                        url = url.back() == ' ' ? url.substr(0, url.size() - 1) : url.substr(1);
                    }
                    if (!userpl::validUrl(url)) {
                        app.toast(tr("playlists.invalid_url"), ToastKind::Error);
                        return;
                    }
                    userpl::Entry e;
                    e.name = n;
                    e.url = url;
                    pl.entries.push_back(e);
                    savePlaylist(app, pl);
                    refreshAll((int) pl.entries.size() - 1);
                })));
            })));
        }

        void confirmDelete() {
            app.push(screens::makeDialog(app, tr("playlists.delete_title"), tr("playlists.delete_text", {pl.title}),
                                         {tr("common.cancel"), tr("common.delete")}, whileAlive([this](int c) {
                if (c == 1) {
                    userpl::remove(pl.path);
                    playlistChanged(app, pl.path);
                    LOG_I("playlists", "playlist deleted");
                    app.pop();
                }
            }), true));
        }

        // keyboard results: nothing happens once this screen is gone
        template<typename F>
        std::function<void(const std::string &)> whileAliveText(F f) {
            auto value = std::make_shared<std::string>();
            std::function<void()> run = guarded([value, f]() { f(*value); });
            return [value, run](const std::string &v) {
                *value = v;
                run();
            };
        }

        userpl::Playlist pl;
        bool moving = false;
        int movedFrom = 0;
        std::vector<Row> views;
        ui::Label *title;
        screens::HeaderContext *context;
        ui::ListView *list;
        ui::EmptyState *empty;
        ui::HintBar *hints;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class MyPlaylistsScreen : public Screen, public ui::ListView::Adapter {
    public:
        explicit MyPlaylistsScreen(App &a) : Screen(a) {
            ui::background(this);
            ui::Label *title = screens::header(this, tr("playlists.title"));
            context = new screens::HeaderContext(this, title);
            list = new ui::ListView(FloatRect(theme::SAFE_X, L::LIST_Y, theme::CONTENT_W, L::LIST_H), ROW_H, 4, this);
            add(list);
            empty = new ui::EmptyState(FloatRect(theme::SAFE_X, L::LIST_Y, theme::CONTENT_W, 420));
            add(empty);
            hints = screens::hintBar(this, {});
            load(0);
        }

        const char *name() const override { return "my-playlists"; }

        void onResume() override {
            load(list->selected());   // an edited / deleted / imported playlist
        }

        int count() override { return (int) items.size(); }

        C2DObject *createRow(float w, float h) override {
            Row r;
            r.bg = new ui::RowFrame(w, h, 0);
            r.kind = ui::label(r.bg, "M3U", theme::LABEL, 28, ui::Label::centerOffset(theme::LABEL, h),   // i18n-exempt
                               ui::Weight::SemiBold, theme::accentText());
            r.name = ui::label(r.bg, "", theme::BODY, 150, ui::Label::centerOffset(theme::BODY, h));
            r.name->setMaxWidth(w - 150 - 460);
            r.meta = ui::label(r.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h), ui::Weight::Regular,
                               theme::textSecondary());
            r.meta->setAlign(ui::Align::Right, w - 28);
            r.meta->setMaxWidth(420);
            views.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int i, bool, bool focused) override {
            for (auto &v: views) {
                if (v.bg != obj) {
                    continue;
                }
                const userpl::Summary &s = items[(size_t) i];
                bool held = moving && focused;
                v.kind->setText(held ? "\xE2\x86\x95" : "M3U");   // i18n-exempt: ↕ on the playlist being moved
                v.name->setText(s.title);
                v.name->setColor(focused ? theme::textStrong() : theme::textPrimary());
                v.name->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                v.meta->setText(!s.editable ? tr("playlists.source_only")
                                            : tr(shown(s) ? "playlists.in_live" : "playlists.not_in_live")
                                              + "  \xC2\xB7  " + entriesText(s.entries));
                v.bg->setState(focused, held);
            }
        }

        void handleInput(const InputEvent &e) override {
            int sel = list->selected();
            int step = e.button == PadButton::Up ? -1 : e.button == PadButton::Down ? 1
                       : e.button == PadButton::L2 ? -list->pageSize() : e.button == PadButton::R2 ? list->pageSize() : 0;
            if (moving) {
                if (step != 0) {
                    int to = std::min(std::max(sel + step, 0), (int) items.size() - 1);
                    if (to != sel) {
                        userpl::Summary held = items[(size_t) sel];
                        items.erase(items.begin() + sel);
                        items.insert(items.begin() + to, held);
                        list->setSelected(to);
                        list->reload();
                    }
                } else if ((e.button == PadButton::Cross || e.button == PadButton::Square) && !e.repeat) {
                    moving = false;
                    saveOrder();   // the Live TV groups follow
                    load(sel);
                } else if (e.button == PadButton::Circle && !e.repeat) {
                    moving = false;
                    load(movedFrom);   // back as it was (nothing saved)
                }
                return;
            }
            if (step != 0) {
                list->moveSelection(step);
                return;
            }
            switch (e.button) {
                case PadButton::Square:
                    if (!e.repeat && items.size() > 1) {
                        moving = true;
                        movedFrom = sel;
                        setHints();
                        list->reload();
                    }
                    return;
                case PadButton::Cross:
                    if (!e.repeat && !items.empty()) {
                        const userpl::Summary &s = items[(size_t) list->selected()];
                        if (s.editable) {
                            app.push(new PlaylistEditorScreen(app, s.path));
                        } else {
                            menu();
                        }
                    }
                    return;
                case PadButton::Options:
                    if (!e.repeat) {
                        menu();
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

    private:
        struct Row {
            ui::RowFrame *bg;
            ui::Label *kind;
            ui::Label *name;
            ui::Label *meta;
        };

        void load(int select) {
            items = usergroups::ordered(app.settings().get().playlistOrder, userpl::list(playlistsDir()));
            context->set("", items.empty() ? "" : i18n::count("playlists.count", (long long) items.size()));
            empty->set(items.empty() ? tr("playlists.empty") : "",
                       items.empty() ? tr("playlists.empty_text", {screens::keyName(A::Options)}) : "");
            list->setSelected(std::min(std::max(select, 0), std::max(0, (int) items.size() - 1)));
            list->reload();
            setHints();
        }

        void setHints() {
            screens::Hints h;
            if (moving) {
                h = {{A::Navigate, tr("playlists.move")}, {A::Confirm, tr("playlists.drop")}, {A::Back, tr("common.cancel")}};
            } else {
                if (!items.empty()) {
                    h.push_back({A::Confirm, tr("common.open")});
                }
                if (items.size() > 1) {
                    h.push_back({A::Edit, tr("playlists.move")});
                }
                h.push_back({A::Options, tr("live.options")});
                h.push_back({A::Back, tr("common.back")});
            }
            hints->setHints(h);
        }

        bool shown(const userpl::Summary &s) const {
            return s.editable && app.settings().get().hiddenPlaylists.count(s.file) == 0;
        }

        // the order on screen becomes the playlists' order (and their Live TV groups' order)
        void saveOrder() {
            std::vector<std::string> order;
            for (const auto &s: items) {
                order.push_back(s.file);
            }
            app.settings().get().playlistOrder = order;
            saveSettings();
            app.applyUserGroups();
        }

        void saveSettings() {
            std::string err;
            if (!app.settings().save(&err)) {
                LOG_E("settings", "save failed: %s", err.c_str());
                app.toast(tr("settings.save_failed"), ToastKind::Error);
            }
        }

        void setShown(const userpl::Summary &s, bool show) {
            std::set<std::string> &hidden = app.settings().get().hiddenPlaylists;
            if (show) {
                hidden.erase(s.file);
            } else {
                hidden.insert(s.file);
            }
            saveSettings();
            app.applyUserGroups();
            app.toast(tr(show ? "playlists.shown_toast" : "playlists.hidden_toast", {s.title}));
            load(list->selected());
        }

        void menu() {
            std::vector<std::string> names;
            std::vector<std::function<void()>> acts;
            if (!items.empty()) {
                userpl::Summary s = items[(size_t) list->selected()];
                if (s.editable) {
                    names.push_back(tr("common.open"));
                    acts.push_back([this, s] { app.push(new PlaylistEditorScreen(app, s.path)); });
                    bool isShown = shown(s);
                    names.push_back(tr(isShown ? "playlists.hide_live" : "playlists.show_live"));
                    acts.push_back([this, s, isShown] { setShown(s, !isShown); });
                    names.push_back(tr("playlists.rename"));
                    acts.push_back([this, s] { rename(s); });
                }
                names.push_back(tr("playlists.use_source"));
                acts.push_back([this, s] { openAsSource(app, s.path, s.title); });
                if (s.editable) {
                    names.push_back(tr("playlists.export"));
                    acts.push_back([this, s] {
                        userpl::Playlist p;
                        std::string err;
                        if (userpl::load(s.path, p, err)) {
                            exportPlaylist(app, p);
                        }
                    });
                }
                names.push_back(tr("playlists.delete"));
                acts.push_back([this, s] { confirmDelete(s); });
            }
            names.push_back(tr("playlists.new"));
            App *a = &app;
            acts.push_back([a] {
                newPlaylist(*a, [a](const userpl::Playlist &p) { a->push(new PlaylistEditorScreen(*a, p.path)); });
            });
            names.push_back(tr("playlists.import"));
            acts.push_back([this] { importFromUsb(); });
            app.push(screens::makeMenu(app, items.empty() ? tr("playlists.title") : items[(size_t) list->selected()].title,
                                       names, -1, whileAlive([acts](int c) {
                if (c >= 0 && c < (int) acts.size()) {
                    acts[(size_t) c]();
                }
            })));
        }

        void rename(const userpl::Summary &s) {
            App *a = &app;
            app.push(screens::makeKeyboard(app, tr("playlists.new_name"), s.title, false, [a, s](const std::string &v) {
                std::string n = userpl::cleanName(v);
                userpl::Playlist p;
                std::string err;
                if (n.empty() || !userpl::load(s.path, p, err)) {
                    return;
                }
                p.title = n;
                savePlaylist(*a, p);   // (this screen reloads in onResume)
            }));
        }

        void confirmDelete(const userpl::Summary &s) {
            app.push(screens::makeDialog(app, tr("playlists.delete_title"), tr("playlists.delete_text", {s.title}),
                                         {tr("common.cancel"), tr("common.delete")}, whileAlive([this, s](int c) {
                if (c == 1 && userpl::inside(playlistsDir(), s.path)) {
                    userpl::remove(s.path);
                    Settings &st = app.settings().get();   // forget its place and visibility
                    st.playlistOrder.erase(std::remove(st.playlistOrder.begin(), st.playlistOrder.end(), s.file),
                                           st.playlistOrder.end());
                    st.hiddenPlaylists.erase(s.file);
                    saveSettings();
                    playlistChanged(app, s.path);
                    LOG_I("playlists", "playlist deleted");
                    load(list->selected());
                }
            }), true));
        }

        // the .m3u / .m3u8 files at the top of every USB drive
        void importFromUsb() {
            std::vector<localmedia::Root> drives = usbRoots();
            if (drives.empty()) {
                app.toast(tr("playlists.no_usb"), ToastKind::Error);
                return;
            }
            localmedia::Formats m3uOnly;
            m3uOnly.addDemuxer("m3u", "m3u,m3u8");   // i18n-exempt: technical value
            std::vector<std::string> names;
            std::vector<std::string> paths;
            for (const auto &d: drives) {
                std::string top;
                localmedia::normalize(d.path, top);
                localmedia::Listing l = localmedia::list(d, top, m3uOnly);
                for (const auto &e: l.entries) {
                    if (!e.directory && localmedia::within(d, e.path) && paths.size() < 64) {
                        names.push_back(driveName(d) + "  \xC2\xB7  " + e.name);
                        paths.push_back(e.path);
                    }
                }
            }
            if (paths.empty()) {
                app.toast(tr("playlists.import_none"), ToastKind::Error);
                return;
            }
            app.push(screens::makeMenu(app, tr("playlists.import"), names, -1, whileAlive([this, paths](int c) {
                if (c >= 0 && c < (int) paths.size()) {
                    importFile(paths[(size_t) c]);
                }
            })));
        }

        void importFile(const std::string &path) {
            std::string name = path.substr(path.find_last_of('/') + 1);
            std::string text;
            std::string err;
            userpl::Playlist p;
            size_t dot = name.rfind('.');
            if (!fs::readFile(path, text, userpl::MAX_FILE_BYTES, &err)
                || !userpl::fromM3u(text, dot != std::string::npos ? name.substr(0, dot) : name, p, err)) {
                LOG_W("playlists", "import refused: %s", err.c_str());
                app.toast(tr("playlists.import_failed", {name, fmt::number((long long) userpl::MAX_ENTRIES)}),
                          ToastKind::Error);
                return;
            }
            p.path = userpl::newPath(playlistsDir(), p.title);
            if (savePlaylist(app, p)) {
                LOG_I("playlists", "imported a playlist of %d entries from USB", (int) p.entries.size());
                app.toast(tr("playlists.imported", {p.title, entriesText((int) p.entries.size())}), ToastKind::Success);
                load(0);
            }
        }

        std::vector<userpl::Summary> items;
        bool moving = false;
        int movedFrom = 0;
        std::vector<Row> views;
        screens::HeaderContext *context;
        ui::ListView *list;
        ui::EmptyState *empty;
        ui::HintBar *hints;
    };
}

namespace screens {

    Screen *makeMyPlaylists(App &app) {
        return new MyPlaylistsScreen(app);
    }

    void addToMyPlaylist(App &app, const userpl::Entry &entry) {
        std::vector<userpl::Summary> all;
        for (auto &s: userpl::list(playlistsDir())) {
            if (s.editable) {
                all.push_back(s);
            }
        }
        std::vector<std::string> names = {tr("playlists.new")};
        std::vector<std::string> paths;
        for (const auto &s: all) {
            names.push_back(s.title);
            paths.push_back(s.path);
        }
        auto add = [&app, entry](userpl::Playlist p) {
            if (userpl::find(p, entry.url) >= 0) {
                app.toast(tr("playlists.already", {p.title}));
                return;
            }
            if (p.entries.size() >= userpl::MAX_ENTRIES) {
                app.toast(tr("playlists.full", {p.title, fmt::number((long long) userpl::MAX_ENTRIES)}), ToastKind::Error);
                return;
            }
            p.entries.push_back(entry);
            if (savePlaylist(app, p)) {
                app.toast(tr("playlists.added", {p.title}), ToastKind::Success);
            }
        };
        app.push(makeMenu(app, tr("playlists.add_to"), names, -1, [&app, paths, add](int c) {
            if (c == 0) {
                newPlaylist(app, add);
            } else if (c > 0 && c <= (int) paths.size()) {
                userpl::Playlist p;
                std::string err;
                if (userpl::load(paths[(size_t) c - 1], p, err)) {
                    add(p);
                } else {
                    app.toast(tr("playlists.save_failed"), ToastKind::Error);
                }
            }
        }));
    }
}
