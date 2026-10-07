// Application: renderer, frame loop with on-demand redraw, navigation stack and services.

#ifndef PS4IPTV_APP_APP_H
#define PS4IPTV_APP_APP_H

#include <string>
#include <vector>

#include "cross2d/c2d.h"
#include "screen.h"
#include "xtream_service.h"
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

// state of the signed-in profile
struct Session {
    bool connected = false;
    iptv::Profile profile;
    iptv::AccountInfo account;
    bool httpsWarning = false;
    std::vector<iptv::Category> categories[3];   // indexed by iptv::ContentType
    bool categoriesLoaded[3] = {false, false, false};
    iptv::LiveCatalog live;
    bool liveLoaded = false;
    std::string liveNotice;                      // e.g. "showing the saved list"
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

    // drawing
    void requestRedraw() { dirty = true; }

    void requestRedrawAt(double when);

    void toast(const std::string &message, ToastKind kind = ToastKind::Info);

    // services
    JobSystem &jobs() { return jobSystem; }

    ProfileStore &profiles() { return profileStore; }

    SettingsStore &settings() { return settingsStore; }

    XtreamService &xtream() { return xtreamService; }

    Session &session() { return currentSession; }

    LibraryStore &library() { return libraryStore; }

    Playback &playback() { return player; }

    // persists favorites/history; logs and toasts on failure
    void saveLibrary();

    double now() const;

    const std::string &romfs() const { return romfsPath; }

private:

    void onUpdate() override;

    void logic();

    void applyNavigation();

    void updateVisibility();

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
    LibraryStore libraryStore;
    Playback player;
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
