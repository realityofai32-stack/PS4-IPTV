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

    // empty profile id = add a new profile
    Screen *makeProfileEdit(App &app, const iptv::Profile &profile);

    Screen *makeConnect(App &app, const iptv::Profile &profile);

    Screen *makeHome(App &app);

    Screen *makeSection(App &app, const std::string &title, const std::string &message);

    // Live TV browser; startOnFavorites opens the Favorites category
    Screen *makeLive(App &app, bool startOnFavorites = false);

    // full-screen live playback; channels = indices into the live catalog used for zapping.
    // onExit(streamId) runs when the user leaves, with the channel that was playing last (focus restore).
    Screen *makeLivePlayer(App &app, const std::vector<int> &channels, int index,
                           std::function<void(const std::string &)> onExit = nullptr);

    Screen *makeSearch(App &app);

    Screen *makeSettings(App &app);

    Screen *makeAbout(App &app);

    // deterministic text rendering check; atStartup: Cross continues to App::firstScreen()
    Screen *makeTextTest(App &app, bool atStartup);

    // modal text entry; onDone(text) only when confirmed with OK
    Screen *makeKeyboard(App &app, const std::string &title, const std::string &initial, bool password,
                         std::function<void(const std::string &)> onDone);

    // modal message with buttons; onChoice(index) when a button is chosen, -1 on Circle (if cancellable)
    Screen *makeDialog(App &app, const std::string &title, const std::string &message,
                       const std::vector<std::string> &buttons, std::function<void(int)> onChoice,
                       bool destructive = false);
}

#endif // PS4IPTV_APP_SCREENS_H
