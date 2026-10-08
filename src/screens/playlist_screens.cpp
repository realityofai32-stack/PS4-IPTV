// M3U / M3U8 playlist sources: the Add Source chooser, the playlist editor and Playlist Info.
//
// The editor downloads and checks a playlist before the source is saved (Save & Open = Test, then save).
// The downloaded playlist becomes the source's saved copy, so opening the new source does not download it a
// second time. The playlist URL may carry credentials or tokens: the form shows m3u::displayUrl() (host and
// file name) and only the keyboard, while editing, shows what was typed.

#include "common.h"
#include "../downloads/download_model.h"
#include "../core/format.h"
#include "../core/url.h"
#include "../iptv/m3u.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"

using namespace c2d;
using namespace iptv;

namespace {

    // "4,281 channels  ·  37 groups"
    std::string channelsAndGroups(const m3u::Info &info) {
        int groups = info.stats.groups;
        return i18n::count("m3u.channels", info.stats.channels) + "  \xC2\xB7  " + i18n::count("m3u.groups", groups);
    }

    // what a test found, for the editor's status panel
    std::string testSummary(const m3u::Info &info) {
        const m3u::Stats &st = info.stats;
        std::string s = tr("playlist.ok") + "\n" + channelsAndGroups(info) + "\n"
                        + tr("playlist.logos", {fmt::number(st.withLogo), fmt::number(st.withoutLogo)});
        if (st.skipped() > 0) {
            s += "\n" + tr("playlist.skipped", {fmt::number(st.skipped())});
        }
        if (st.https > 0) {
            s += "\n\n" + i18n::count("playlist.https_warning", st.https);
        }
        return s;
    }

    // printable ASCII only: a User-Agent goes into an HTTP header line
    bool validUserAgent(const std::string &ua) {
        if (ua.size() > 256) {
            return false;
        }
        for (unsigned char c: ua) {
            if (c < 0x20 || c > 0x7E) {
                return false;
            }
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class PlaylistEditScreen : public Screen {
    public:
        PlaylistEditScreen(App &a, const Profile &p) : Screen(a), profile(p), original(p) {
            isNew = p.id.empty();
            profile.type = SourceType::M3u;
            ui::background(this);
            screens::header(this, tr(isNew ? "playlist.add_title" : "playlist.edit_title"), tr("playlist.subtitle"));
            const char *captions[FIELDS] = {"playlist.name", "playlist.url", "playlist.user_agent"};
            for (int i = 0; i < FIELDS; i++) {
                fields[i] = new screens::FieldRow(FloatRect(theme::SAFE_X, 230 + (float) i * 104, 1000, 88),
                                                  tr(captions[i]));
                add(fields[i]);
            }
            const char *labels[BUTTONS] = {"playlist.test", "playlist.save_open", "common.cancel"};
            float x = theme::SAFE_X;
            for (int i = 0; i < BUTTONS; i++) {
                float w = i == 2 ? 220.0f : 340.0f;
                buttons[i] = new ui::Button(tr(labels[i]), FloatRect(x, 586, w, 84), i == 1);
                add(buttons[i]);
                x += w + 24;
            }
            status = ui::box(this, FloatRect(1160, 230, 664, 544), theme::surface(), theme::RADIUS);
            statusTitle = ui::label(status, tr("playlist.status_title"), theme::HEADING, 40, 36, ui::Weight::SemiBold);
            statusBody = ui::label(status, "", theme::BODY, 40, 100, ui::Weight::Regular, theme::textDim());
            statusBody->setMaxWidth(584);
            statusBody->setMaxLines(11);
            spinner = new ui::Spinner(18);
            spinner->setPosition(40, 110);
            spinner->setVisibility(Visibility::Hidden);
            status->add(spinner);
            screens::hintBar(this, {{ui::Glyph::Cross, tr("profile.hint_edit")}, {ui::Glyph::Circle, tr("common.cancel")}});
            setStatus(tr(isNew ? "playlist.status_new" : "playlist.status_edit"), theme::textDim());
            refresh();
        }

        ~PlaylistEditScreen() override {
            testToken.cancel();
        }

        const char *name() const override { return "playlist-edit"; }

        void tick(double now) override {
            if (testing && spinner->tick(now)) {
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            switch (e.button) {
                case PadButton::Up:
                    focus = focus >= FIELDS ? FIELDS - 1 : std::max(0, focus - 1);
                    break;
                case PadButton::Down:
                    focus = std::min(focus + 1, FIELDS);
                    break;
                case PadButton::Left:
                    if (focus > FIELDS) {
                        focus--;
                    }
                    break;
                case PadButton::Right:
                    if (focus >= FIELDS && focus < FIELDS + BUTTONS - 1) {
                        focus++;
                    }
                    break;
                case PadButton::Cross:
                    if (!e.repeat) {
                        activate();
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
            refresh();
        }

    private:
        static constexpr int FIELDS = 3;
        static constexpr int BUTTONS = 3;

        void activate() {
            switch (focus) {
                case 0:
                    edit(tr("playlist.name"), profile.name, [this](const std::string &v) { profile.name = v; });
                    break;
                case 1:
                    edit(tr("playlist.url"), profile.playlistUrl, [this](const std::string &v) { profile.playlistUrl = v; });
                    break;
                case 2:
                    edit(tr("playlist.user_agent"), profile.userAgent, [this](const std::string &v) { profile.userAgent = v; });
                    break;
                case FIELDS:
                    test(false);
                    break;
                case FIELDS + 1:
                    test(true);
                    break;
                default:
                    app.pop();
                    break;
            }
        }

        void edit(const std::string &title, const std::string &value, std::function<void(const std::string &)> set) {
            app.push(screens::makeKeyboard(app, title, value, false, [this, set](const std::string &v) {
                set(v);
                tested.reset();
                refresh();
            }));
        }

        bool locationChanged() const {
            return isNew || url::trim(profile.playlistUrl) != original.playlistUrl
                   || url::trim(profile.userAgent) != original.userAgent;
        }

        // validates the form into `profile`; returns an error message or ""
        std::string validate() {
            std::string location = url::trim(profile.playlistUrl);
            std::string local = m3u::localPlaylistPath(location);
            if (!local.empty()) {
                location = local;
            } else if (!m3u::isHttpUrl(location)) {
                return tr("playlist.error.url");
            }
            std::string ua = url::trim(profile.userAgent);
            if (!validUserAgent(ua)) {
                return tr("playlist.error.user_agent");
            }
            profile.playlistUrl = location;
            profile.userAgent = ua;
            profile.name = url::trim(profile.name);
            if (profile.name.empty()) {
                std::string shown = m3u::displayUrl(location);
                profile.name = shown.substr(0, shown.find('/'));
            }
            return "";
        }

        void test(bool thenSave) {
            if (testing) {
                return;
            }
            std::string err = validate();
            if (!err.empty()) {
                setStatus(err, theme::danger());
                refresh();
                return;
            }
            // only the name changed: nothing to download again
            if (thenSave && (tested || !locationChanged())) {
                save();
                return;
            }
            testing = true;
            spinner->setVisibility(Visibility::Visible);
            setStatus("", theme::textDim());
            statusTitle->setText(tr("playlist.testing"));
            LOG_I("profiles", "testing playlist %s", m3u::displayUrl(profile.playlistUrl).c_str());
            testToken = app.m3u().load(profile, APP_DATA_DIR, M3uService::Source::Network, false,
                                       [this, thenSave](M3uService::Outcome &o) {
                testing = false;
                spinner->setVisibility(Visibility::Hidden);
                statusTitle->setText(tr("playlist.status_title"));
                if (o.ok) {
                    tested = o.body;
                    testedChannels = (int) o.catalog->channels().size();
                    setStatus(testSummary(o.info), o.info.stats.https > 0 ? theme::warning() : theme::success());
                    if (thenSave) {
                        save();
                    }
                } else {
                    tested.reset();
                    setStatus(o.message + (thenSave ? "\n\n" + tr("playlist.not_saved") : ""), theme::danger());
                }
                refresh();
            }, true);
            refresh();
        }

        void save() {
            Profile p = profile;
            bool changed = locationChanged();
            int64_t now = clockx::unixNow();
            std::string id = app.profiles().upsert(p, now);
            app.profiles().setActive(id);
            std::string err;
            if (!app.profiles().save(&err)) {
                LOG_E("profiles", "save failed: %s", err.c_str());
                setStatus(tr("profile.save_failed"), theme::danger());
                return;
            }
            if (tested) {
                // the playlist just checked becomes the saved copy: opening does not download it again
                if (M3uService::saveBody(APP_DATA_DIR, id, *tested, testedChannels, now)) {
                    app.notePlaylistLoaded(id, testedChannels);
                }
            } else if (changed) {
                CatalogCache(APP_DATA_DIR).remove(id, M3uService::CACHE_NAME);   // never show another URL's list
            }
            LOG_I("profiles", "playlist source %s saved (%s)", id.c_str(), m3u::displayUrl(p.playlistUrl).c_str());
            app.replaceAll(screens::makeConnect(app, *app.profiles().find(id)));
        }

        void setStatus(const std::string &text, const Color &color) {
            statusBody->setText(text);
            statusBody->setColor(color);
        }

        void refresh() {
            std::string shown = m3u::displayUrl(url::trim(profile.playlistUrl));
            fields[0]->setValue(profile.name.empty() ? tr("profile.name_placeholder") : profile.name, profile.name.empty());
            if (profile.playlistUrl.empty()) {
                fields[1]->setValue(tr("playlist.url_placeholder"), true);
            } else {
                fields[1]->setValue(shown.empty() ? tr("playlist.url_invalid") : shown, false);
            }
            fields[2]->setValue(profile.userAgent.empty() ? tr("playlist.user_agent_default") : profile.userAgent,
                                profile.userAgent.empty());
            for (int i = 0; i < FIELDS; i++) {
                fields[i]->setFocused(focus == i);
            }
            for (int i = 0; i < BUTTONS; i++) {
                buttons[i]->setFocused(focus == FIELDS + i);
                buttons[i]->setEnabled(!testing || i == 2);
            }
        }

        Profile profile;
        Profile original;
        bool isNew;
        bool testing = false;
        std::shared_ptr<std::string> tested;   // the playlist downloaded by the last successful test
        int testedChannels = 0;
        int focus = 0;
        screens::FieldRow *fields[FIELDS];
        ui::Button *buttons[BUTTONS];
        RectangleShape *status;
        ui::Label *statusTitle;
        ui::Label *statusBody;
        ui::Spinner *spinner;
        CancelToken testToken;
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////
    class PlaylistInfoScreen : public Screen {
    public:
        explicit PlaylistInfoScreen(App &a) : Screen(a) {
            ui::background(this);
            screens::header(this, tr("m3u.info"), app.session().profile.name);
            panel = ui::box(this, FloatRect(theme::SAFE_X, 200, theme::SCREEN_W - 2 * theme::SAFE_X, 760),
                            theme::surface(), theme::RADIUS);
            for (int i = 0; i < ROWS; i++) {
                float y = 34 + (float) i * 50;
                captions[i] = ui::label(panel, "", theme::BODY, 48, y, ui::Weight::Regular, theme::textDim());
                captions[i]->setMaxWidth(760);
                values[i] = ui::label(panel, "", theme::BODY, 0, y, ui::Weight::SemiBold);
                values[i]->setAlign(ui::Align::Right, panel->getSize().x - 48);
                values[i]->setMaxWidth(900);
            }
            screens::hintBar(this, {{ui::Glyph::Cross, tr("m3u.refresh")}, {ui::Glyph::Circle, tr("common.back")}});
            fill();
        }

        const char *name() const override { return "playlist-info"; }

        void tick(double) override {
            if (app.session().liveGeneration != generation) {
                fill();
                redraw();
            }
        }

        void handleInput(const InputEvent &e) override {
            if (e.repeat) {
                return;
            }
            if (e.button == PadButton::Cross || e.button == PadButton::Triangle) {
                app.refreshPlaylist(true);
                fill();
            } else if (e.button == PadButton::Circle) {
                app.pop();
            }
        }

    private:
        static constexpr int ROWS = 14;

        void fill() {
            const Session &s = app.session();
            generation = s.liveGeneration;
            const m3u::Info &info = s.playlist;
            const m3u::Stats &st = info.stats;
            int uncategorized = s.live.countInCategory(UNCATEGORIZED_ID);
            std::string refreshed = info.savedAt > 0 ? clockx::localDate(info.savedAt) : tr("m3u.never");
            if (s.playlistRefreshing) {
                refreshed = tr("m3u.refreshing");
            }
            std::vector<std::pair<std::string, std::string>> rows = {
                    {tr("m3u.info_source"), screens::sourceLocation(s.profile)},
                    {tr("m3u.info_channels"), fmt::number((long long) s.live.channels().size())},
                    {tr("m3u.info_groups"), fmt::number(st.groups)},
                    {tr("catalog.uncategorized"), fmt::number(uncategorized)},
                    {tr("m3u.info_with_logos"), fmt::number(st.withLogo)},
                    {tr("m3u.info_without_logos"), fmt::number(st.withoutLogo)},
                    {tr("m3u.info_skipped"), st.skipped() == 0 ? fmt::number(0)
                                                               : tr("m3u.info_skipped_detail",
                                                                    {fmt::number(st.skipped()), fmt::number(st.missingUrl),
                                                                     fmt::number(st.invalidUrl + st.overLimit)})},
                    {tr("m3u.info_repaired"), fmt::number(st.malformedExtinf + st.invalidUtf8)},
                    {tr("m3u.info_hls"), fmt::number(st.hls)},
                    {tr("m3u.info_https"), fmt::number(st.https)},
                    {tr("m3u.info_other"), fmt::number(st.otherProtocols)},
                    {tr("m3u.info_user_agents"), fmt::number(st.userAgents)},
                    {tr("m3u.info_last_refresh"), refreshed},
                    {tr("m3u.info_size"), st.bytes > 0 ? dl::formatBytes((int64_t) st.bytes) : "-"}};
            for (int i = 0; i < ROWS; i++) {
                captions[i]->setText(i < (int) rows.size() ? rows[(size_t) i].first : "");
                values[i]->setText(i < (int) rows.size() ? rows[(size_t) i].second : "");
            }
            // HTTPS streams cannot play in this version: flagged, not hidden
            values[9]->setColor(st.https > 0 ? theme::warning() : theme::text());
            values[6]->setColor(st.skipped() > 0 ? theme::warning() : theme::text());
        }

        RectangleShape *panel;
        ui::Label *captions[ROWS];
        ui::Label *values[ROWS];
        unsigned generation = 0;
    };
}

namespace screens {
    Screen *makeSourceChooser(App &app) {
        return makeMenu(app, tr("source.add_title"), {tr("source.type_xtream"), tr("source.type_m3u")}, -1,
                        [&app](int choice) {
                            if (choice == 0) {
                                app.push(makeProfileEdit(app, Profile()));
                            } else if (choice == 1) {
                                Profile p;
                                p.type = SourceType::M3u;
                                app.push(makePlaylistEdit(app, p));
                            }
                        }, {tr("source.type_xtream_detail"), tr("source.type_m3u_detail")});
    }

    Screen *makePlaylistEdit(App &app, const Profile &profile) {
        return new PlaylistEditScreen(app, profile);
    }

    Screen *makePlaylistInfo(App &app) {
        return new PlaylistInfoScreen(app);
    }
}
