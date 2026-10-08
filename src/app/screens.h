// Screen factories (keeps screen classes private to their .cpp files).

#ifndef PS4IPTV_APP_SCREENS_H
#define PS4IPTV_APP_SCREENS_H

#include <functional>
#include <string>
#include <vector>

#include "../iptv/models.h"

class App;

class Screen;

namespace screens {

    Screen *makeOnboarding(App &app);

    Screen *makeProfiles(App &app);

    // Add Source: Xtream Codes / M3U / M3U8 Playlist, then the matching editor
    Screen *makeSourceChooser(App &app);

    // empty profile id = add a new profile; playlist profiles open the playlist editor
    Screen *makeProfileEdit(App &app, const iptv::Profile &profile);

    // M3U / M3U8 playlist source: name, playlist URL, optional User-Agent; tested before it is saved
    Screen *makePlaylistEdit(App &app, const iptv::Profile &profile);

    // counts of the playlist on screen (never URLs or credentials) + Refresh Playlist
    Screen *makePlaylistInfo(App &app);

    Screen *makeConnect(App &app, const iptv::Profile &profile);

    Screen *makeHome(App &app);

    Screen *makeSection(App &app, const std::string &title, const std::string &message);

    // Live TV browser; startOnFavorites opens the Favorites category
    Screen *makeLive(App &app, bool startOnFavorites = false);

    // full-screen live playback; channels = indices into the live catalog used for zapping.
    // onExit(streamId) runs when the user leaves, with the channel that was playing last (focus restore).
    Screen *makeLivePlayer(App &app, const std::vector<int> &channels, int index,
                           std::function<void(const std::string &)> onExit = nullptr);

    // ------------------------------------------------------------------ Movies / Series
    // One playable movie or episode (everything the player and the progress store need)
    struct VodItem {
        iptv::ContentType type = iptv::ContentType::Movie;   // Movie, or Series for an episode
        std::string id;               // stream id / episode id
        std::string extension;        // container_extension
        std::string title;            // movie title / episode title ("" = "Episode N")
        std::string image;            // poster / series cover
        int year = 0;
        std::string seriesId;
        std::string seriesName;
        int season = 0;
        int episode = 0;
        double durationHint = 0;      // from the provider metadata, until mpv knows better
        // offline playback: the downloaded file (played instead of the provider URL) and the profile the
        // download belongs to (its progress is kept there). Both "" when streaming in the signed-in profile.
        std::string localPath;
        std::string profileId;
    };

    // categories + poster grid; opening loads the catalog lazily (saved copy first)
    Screen *makeMovies(App &app);

    Screen *makeSeries(App &app);

    Screen *makeMovieDetail(App &app, const iptv::Movie &movie);

    Screen *makeSeriesDetail(App &app, const iptv::Series &series);

    // plays queue[index]; resume = continue from the saved position. Episodes: the rest of the queue
    // is offered as "next episode". onExit(id of the item playing last) runs when the user leaves.
    Screen *makeVodPlayer(App &app, const std::vector<VodItem> &queue, int index, bool resume,
                          std::function<void(const std::string &)> onExit = nullptr);

    // favorites of all types (Live channels, Movies, Series)
    Screen *makeFavorites(App &app);

    // Search across Live TV, Movies and Series; filter: 0 All, 1 Movies, 2 Series, 3 Live TV
    Screen *makeSearch(App &app, int filter = 0);

    enum class SettingsPage {
        Main,
        Storage,        // Storage & downloads: sizes, free space, deleting downloads and caches
        Diagnostics     // text rendering test, download diagnostics, build information
    };

    Screen *makeSettings(App &app, SettingsPage page = SettingsPage::Main);

    Screen *makeAbout(App &app);

    // measurements of the download in progress / the last one (speeds, callbacks, flushes; no URL)
    Screen *makeDownloadDiagnostics(App &app);

    // Downloads: in progress / downloaded movies and episodes (works offline, from the manifest)
    Screen *makeDownloads(App &app);

    // deterministic text rendering check; atStartup: Cross continues to App::firstScreen()
    Screen *makeTextTest(App &app, bool atStartup);

    // modal text entry; onDone(text) only when confirmed with OK
    Screen *makeKeyboard(App &app, const std::string &title, const std::string &initial, bool password,
                         std::function<void(const std::string &)> onDone);

    // modal message with buttons; onChoice(index) when a button is chosen, -1 on Circle (if cancellable)
    // modal list in a side panel (Options menus): onChoice(index) on X, -1 on Circle. checked: index shown
    // with a check mark (-1 = none); details: optional second line per option
    Screen *makeMenu(App &app, const std::string &title, const std::vector<std::string> &options, int checked,
                     std::function<void(int)> onChoice, const std::vector<std::string> &details = {});

    Screen *makeDialog(App &app, const std::string &title, const std::string &message,
                       const std::vector<std::string> &buttons, std::function<void(int)> onChoice,
                       bool destructive = false);
}

#endif // PS4IPTV_APP_SCREENS_H
