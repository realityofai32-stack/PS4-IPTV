// Local Media: the console's own storage as a simple media browser (media/local_media.h). The first level lists the
// storage that can be opened right now (internal /data, USB drives); folders and playable files follow, folders
// first. A file plays in the movie player (same backend, tracks, subtitles, subtitle style, aspect / crop / zoom,
// Options panel), outside every source: nothing goes to history, Continue Watching, favorites or search.
//
// Cross opens a folder / plays a file, Circle goes one folder up (from the storage list: back), Triangle reads the
// folder again (a USB drive plugged in meanwhile appears in the storage list).

#include "common.h"
#include "../downloads/download_model.h"
#include "../media/local_media.h"
#include "../platform/log.h"

using namespace c2d;

namespace {

    namespace L = layout::downloads;

    const float ROW_H = 96;

    class LocalMediaScreen : public Screen, public ui::ListView::Adapter {
    public:
        explicit LocalMediaScreen(App &a) : Screen(a), formats(localmedia::ffmpegFormats()) {
            ui::background(this);
            ui::Label *title = screens::header(this, tr("localmedia.title"));
            context = new screens::HeaderContext(this, title);
            list = new ui::ListView(FloatRect(theme::SAFE_X, L::LIST_Y, theme::CONTENT_W, L::LIST_H), ROW_H, 4, this);
            add(list);
            empty = new ui::EmptyState(FloatRect(theme::SAFE_X, L::LIST_Y, theme::CONTENT_W, 420));
            add(empty);
            hints = screens::hintBar(this, {});
            LOG_I("localmedia", "%d playable extensions from the playback backend", (int) formats.size());
            showRoots();
        }

        const char *name() const override { return "local-media"; }

        int count() override { return (int) rows.size(); }

        C2DObject *createRow(float w, float h) override {
            Row r;
            r.bg = new ui::RowFrame(w, h, 0);
            r.kind = ui::label(r.bg, "", theme::LABEL, 28, ui::Label::centerOffset(theme::LABEL, h), ui::Weight::SemiBold,
                               theme::accentText());
            r.name = ui::label(r.bg, "", theme::BODY, 150, ui::Label::centerOffset(theme::BODY, h));
            r.name->setMaxWidth(w - 150 - 320);
            r.meta = ui::label(r.bg, "", theme::LABEL, 0, ui::Label::centerOffset(theme::LABEL, h), ui::Weight::Regular,
                               theme::textSecondary());
            r.meta->setAlign(ui::Align::Right, w - 28);
            r.meta->setMaxWidth(280);
            views.push_back(r);
            return r.bg;
        }

        void bindRow(C2DObject *obj, int i, bool, bool focused) override {
            for (auto &v: views) {
                if (v.bg != obj) {
                    continue;
                }
                const Item &it = rows[(size_t) i];
                v.kind->setText(it.kind);
                v.name->setText(it.name);
                v.name->setColor(focused ? theme::textStrong() : theme::textPrimary());
                v.name->setWeight(focused ? ui::Weight::SemiBold : ui::Weight::Regular);
                v.meta->setText(it.meta);
                v.bg->setState(focused);
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    list->moveSelection(-1);
                    return;
                case PadButton::Down:
                    list->moveSelection(1);
                    return;
                case PadButton::L2:
                    list->moveSelection(-list->pageSize());
                    return;
                case PadButton::R2:
                    list->moveSelection(list->pageSize());
                    return;
                case PadButton::Cross:
                    if (!e.repeat && !rows.empty()) {
                        openRow(list->selected());
                    }
                    return;
                case PadButton::Triangle:
                    if (!e.repeat) {
                        if (root == nullptr) {
                            showRoots();
                        } else {
                            showFolder(folder, list->selected());
                        }
                    }
                    return;
                case PadButton::Circle:
                    if (!e.repeat) {
                        up();
                    }
                    return;
                default:
                    return;
            }
        }

    private:
        struct Item {
            std::string kind;      // "USB" / folder / file type, short
            std::string name;
            std::string meta;
            std::string path;
            bool directory = false;
            int root = -1;         // storage list: index into `roots`
        };

        struct Row {
            ui::RowFrame *bg;
            ui::Label *kind;
            ui::Label *name;
            ui::Label *meta;
        };

        std::string rootName(const localmedia::Root &r) const {
            return r.usb ? tr("localmedia.usb", {std::to_string(r.number)}) : tr("localmedia.internal");
        }

        void showRoots() {
            root = nullptr;
            folder.clear();
            positions.clear();
            roots.clear();
            rows.clear();
            roots = localmedia::detectRoots();
            for (size_t i = 0; i < roots.size(); i++) {
                Item it;
                it.kind = roots[i].usb ? "USB" : tr("localmedia.kind_storage");   // i18n-exempt: USB is a proper name
                it.name = rootName(roots[i]);
                it.meta = roots[i].path;
                it.path = roots[i].path;
                it.directory = true;
                it.root = (int) i;
                rows.push_back(it);
            }
            LOG_I("localmedia", "%d storage location(s) available", (int) roots.size());
            context->set("", "");
            empty->set(rows.empty() ? tr("localmedia.no_storage") : "", rows.empty() ? tr("localmedia.no_storage_text") : "");
            list->setSelected(0);
            list->reload();
            setHints();
        }

        // folder: a normalized path inside the open root
        void showFolder(const std::string &path, int select = 0) {
            localmedia::Listing l = localmedia::list(*root, path, formats);
            folder = path;
            rows.clear();
            if (l.ok) {
                for (const auto &e: l.entries) {
                    Item it;
                    it.name = e.name;
                    it.path = e.path;
                    it.directory = e.directory;
                    if (e.directory) {
                        it.kind = tr("localmedia.kind_folder");
                    } else {
                        std::string ext = localmedia::extensionOf(e.name);
                        for (char &c: ext) {
                            c = (char) std::toupper((unsigned char) c);
                        }
                        it.kind = ext;
                        it.meta = e.size >= 0 ? dl::formatBytes(e.size) : "";
                    }
                    rows.push_back(it);
                }
            }
            // "USB 1  ·  Movies / Action"
            std::string rootPath;
            localmedia::normalize(root->path, rootPath);
            std::string inside = path.size() > rootPath.size() ? path.substr(rootPath.size() + 1) : "";
            for (size_t i = 0; i < inside.size(); i++) {
                if (inside[i] == '/') {
                    inside.replace(i, 1, " / ");
                    i += 2;
                }
            }
            context->set(rootName(*root), inside);
            if (!l.ok) {
                LOG_W("localmedia", "folder could not be opened");
                empty->set(tr("localmedia.unreadable"), tr("localmedia.unreadable_text"));
            } else if (rows.empty()) {
                empty->set(tr("localmedia.empty"), tr("localmedia.empty_text"));
            } else {
                empty->set("", "");
            }
            list->setSelected(std::min(std::max(0, select), std::max(0, (int) rows.size() - 1)));
            list->reload();
            setHints();
        }

        void openRow(int i) {
            if (i < 0 || i >= (int) rows.size()) {
                return;
            }
            const Item it = rows[(size_t) i];
            if (root == nullptr) {
                rootIndex = it.root;
                root = &roots[(size_t) rootIndex];
                std::string start;
                localmedia::normalize(root->path, start);
                showFolder(start);
                return;
            }
            if (!localmedia::within(*root, it.path)) {
                return;   // never outside the open storage
            }
            if (it.directory) {
                positions.push_back(i);
                showFolder(it.path);
                return;
            }
            play(it);
        }

        void up() {
            if (root == nullptr) {
                app.pop();
                return;
            }
            std::string rootPath;
            localmedia::normalize(root->path, rootPath);
            if (folder == rootPath) {
                int at = rootIndex;
                showRoots();
                list->setSelected(at);
                return;
            }
            std::string parent = folder.substr(0, folder.rfind('/'));
            if (parent.empty() || !localmedia::within(*root, parent)) {
                parent = rootPath;
            }
            int select = positions.empty() ? 0 : positions.back();
            if (!positions.empty()) {
                positions.pop_back();
            }
            showFolder(parent, select);
        }

        void play(const Item &it) {
            screens::VodItem v;
            v.type = iptv::ContentType::Movie;
            v.id = "local:" + it.path;   // i18n-exempt: technical value
            v.localPath = it.path;
            v.localMedia = true;
            v.extension = localmedia::extensionOf(it.name);
            size_t dot = it.name.rfind('.');
            v.title = dot != std::string::npos && dot > 0 ? it.name.substr(0, dot) : it.name;
            LOG_I("localmedia", "playing a local %s file", v.extension.c_str());
            app.push(screens::makeVodPlayer(app, {v}, 0, false));
        }

        void setHints() {
            if (root == nullptr) {
                hints->setHints({{ui::Glyph::Cross, tr("common.open")}, {ui::Glyph::Triangle, tr("localmedia.rescan")},
                                 {ui::Glyph::Circle, tr("common.back")}});
            } else {
                hints->setHints({{ui::Glyph::Cross, tr("localmedia.open_play")},
                                 {ui::Glyph::Triangle, tr("localmedia.refresh")},
                                 {ui::Glyph::Circle, tr("localmedia.up")}});
            }
        }

        localmedia::Formats formats;
        std::vector<localmedia::Root> roots;
        const localmedia::Root *root = nullptr;
        int rootIndex = 0;
        std::string folder;
        std::vector<int> positions;   // selected row of every folder above, to come back to it
        std::vector<Item> rows;
        std::vector<Row> views;
        screens::HeaderContext *context;
        ui::ListView *list;
        ui::EmptyState *empty;
        ui::HintBar *hints;
    };
}

namespace screens {
    Screen *makeLocalMedia(App &app) {
        return new LocalMediaScreen(app);
    }
}
