// Application: renderer, frame loop with on-demand redraw, navigation stack and services.

#ifndef PS4IPTV_APP_APP_H
#define PS4IPTV_APP_APP_H

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cross2d/c2d.h"
#include "m3u_service.h"
#include "screen.h"
#include "vod_library.h"
#include "xtream_service.h"
#include "../images/image_loader.h"
#include "../downloads/curl_transport.h"
#include "../downloads/download_manager.h"
#include "../iptv/catalog.h"
#include "../iptv/models.h"
#include "../network/jobs.h"
#include "../platform/input.h"
#include "../player/playback.h"
#include "../storage/library_store.h"
#include "../storage/profile_store.h"
#include "../storage/settings_store.h"

#define APP_DATA_DIR "/data/PS4IPTV/"

namespace ui {
    class Label;
}

// state of the signed-in profile (source)
struct Session {
    Session();

    unsigned serial;                             // differs for every sign-in: late callbacks check it
    bool connected = false;
    // the provider could not be reached at sign-in and the user continued with saved lists and downloads
    bool offline = false;
    iptv::Profile profile;
    iptv::AccountInfo account;
    bool httpsWarning = false;
    std::vector<iptv::Category> categories[3];   // indexed by iptv::ContentType
    bool categoriesLoaded[3] = {false, false, false};
    iptv::LiveCatalog live;
    bool liveLoaded = false;
    std::string liveNotice;                      // e.g. "showing the saved list"
    std::string learnedLiveFormat;               // Auto format: "ts"/"m3u8" that played after a fallback
    std::set<std::string> seenLanguages;         // track languages met in played files (offered in Settings)

    // the channel list changed (playlist refresh): screens holding indices into `live` rebuild
    unsigned liveGeneration = 0;
    // live player screens open: they hold indices into `live`, so a refreshed playlist waits until they close
    int livePlayers = 0;
    // playlist sources
    m3u::Info playlist;                          // what the current list is (Playlist Info)
    bool playlistRefreshing = false;
    std::shared_ptr<iptv::LiveCatalog> pendingLive;   // refreshed list waiting for livePlayers == 0
    m3u::Info pendingPlaylist;
};

enum class ToastKind {
    Info,
    Success,
    Error
};

class App : public c2d::C2DRenderer {

public:

    App();

    ~App() override;

    void run();

    void quit() { running = false; }

    // navigation (changes take effect between frames)
    void push(Screen *screen);

    void pop();

    void replaceAll(Screen *screen);

    Screen *top() const { return stack.empty() ? nullptr : stack.back(); }

    // onboarding, connect to the active profile, or the profile list
    Screen *firstScreen();

    // drawing
    void requestRedraw() { dirty = true; }

    void requestRedrawAt(double when);

    void toast(const std::string &message, ToastKind kind = ToastKind::Info);

    // services
    JobSystem &jobs() { return jobSystem; }

    ProfileStore &profiles() { return profileStore; }

    SettingsStore &settings() { return settingsStore; }

    XtreamService &xtream() { return xtreamService; }

    M3uService &m3u() { return m3uService; }

    // Movies / Series catalogs (lazy) and their detail caches
    VodLibrary &vod() { return vodLibrary; }

    Session &session() { return currentSession; }

    // a new sign-in starts: forgets the current source's session (pending playlist refreshes are dropped)
    void resetSession();

    // playlist sources: replaces the channel list now, or once no live player holds indices into it
    void setPlaylist(std::shared_ptr<iptv::LiveCatalog> catalog, const m3u::Info &info);

    // playlist sources: downloads the playlist again in the background. The list on screen is replaced only
    // by a valid new playlist; on failure it stays (manual: the outcome is toasted).
    void refreshPlaylist(bool manual);

    // stores a playlist source's channel count / status after a load (Profiles shows it)
    void notePlaylistLoaded(const std::string &profileId, int channels);

    LibraryStore &library() { return libraryStore; }

    Playback &playback() { return player; }

    ImageLoader &images() { return imageLoader; }

    // offline downloads (one transfer at a time on its own thread)
    dl::DownloadManager &downloads() { return *downloadManager; }

    // gives the download manager the current profiles (credentials stay in memory); call after every change
    // of the profile list
    void syncDownloadProfiles();

    // a video player opened / closed: active downloads pause meanwhile (playback stability first)
    void setPlaybackActive(bool active);

    // applies Settings > Language and rebuilds the screens, so every label is in the new language
    void applyLanguage(bool rebuildScreens);

    // persists favorites/history; logs and toasts on failure
    void saveLibrary();

    double now() const;

    const std::string &romfs() const { return romfsPath; }

private:

    void onUpdate() override;

    void logic();

    void applyNavigation();

    void updateVisibility();

    void applyPendingPlaylist();

    bool running = true;
    bool inDrawPass = false;
    bool dirty = true;
    double redrawAt = 0;
    double lastDraw = 0;
    bool forceContinuousRedraw = false;

    std::string romfsPath;
    JobSystem jobSystem;
    ProfileStore profileStore;
    SettingsStore settingsStore;
    XtreamService xtreamService;
    M3uService m3uService;
    CancelToken playlistToken;
    VodLibrary vodLibrary;
    LibraryStore libraryStore;
    Playback player;
    ImageLoader imageLoader;
    std::unique_ptr<dl::CurlTransport> downloadTransport;
    std::unique_ptr<dl::DownloadManager> downloadManager;
    Session currentSession;
    InputManager input;
    void *ownPad = nullptr;          // pad opened by us when libcross2d had none at startup
    double nextPadProbe = 0;

    c2d::RectangleShape *screenLayer = nullptr;
    c2d::RectangleShape *toastBox = nullptr;
    ui::Label *toastText = nullptr;
    double toastUntil = 0;

    std::vector<Screen *> stack;
    std::vector<std::pair<int, Screen *>> pendingOps;  // 0 push, 1 pop, 2 replaceAll
    std::vector<Screen *> graveyard;
};

#endif // PS4IPTV_APP_APP_H
