// PS4 IPTV - entry point.
// Start/exit sequence follows pPlay's main(): network module, renderer, loop, sceSystemServiceLoadExec("exit").

#include <SDL2/SDL.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>

#include "app/app.h"
#include "build_info.h"
#include "platform/log.h"

static void sdlLogOutput(void *, int category, SDL_LogPriority priority, const char *message) {
    diag::Level level = priority >= SDL_LOG_PRIORITY_ERROR ? diag::Level::Error
                                                           : priority == SDL_LOG_PRIORITY_WARN ? diag::Level::Warn
                                                                                               : diag::Level::Info;
    diag::write(level, "sdl", diag::format("[cat %d] %s", category, message ? message : ""));
}

int main() {
    diag::init(APP_DATA_DIR "logs/");
#ifndef NDEBUG
    diag::setVerbose(true);
#endif
    diag::FileStatus fs = diag::fileStatus();
    LOG_I("app", "PS4 IPTV v%s starting (title %s, build %s, git %s, %s)", APP_VERSION, APP_TITLE_ID, BUILD_DATE,
          BUILD_GIT_HASH, BUILD_TYPE);
    if (!fs.ok) {
        LOG_E("storage", "log file %s not writable: %s", fs.path.c_str(), fs.detail.c_str());
    }

    SDL_LogSetOutputFunction(sdlLogOutput, nullptr);
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);

    uint32_t net = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if (net != 0) {
        LOG_E("platform", "sceSysmoduleLoadModuleInternal(NET) failed: 0x%08x", net);
    }

    auto *app = new App();
    app->run();
    delete app;

    LOG_I("app", "exit");
    diag::shutdown();
    sceSystemServiceLoadExec("exit", nullptr);
    while (true) {}
    return 0;
}
