// PS4 IPTV playback test - entry point.
//
// Same start/stop sequence as pPlay's main() (src/main.cpp @ 0399546):
//   sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) -> renderer -> loop -> delete
//   -> sceSystemServiceLoadExec("exit", nullptr)
// with the diagnostic log started before anything else.

#include <sys/time.h>
#include <ctime>

#include <SDL2/SDL.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>

#include "app.h"
#include "build_info.h"
#include "diag_log.h"

static void sdlLogOutput(void *, int category, SDL_LogPriority priority, const char *message) {
    diag::Level level = priority >= SDL_LOG_PRIORITY_ERROR ? diag::Level::Error
                                                           : priority == SDL_LOG_PRIORITY_WARN ? diag::Level::Warn
                                                                                               : diag::Level::Info;
    diag::write(level, "sdl", diag::format("[cat %d] %s", category, message ? message : ""));
}

int main() {
    diag::init(APP_DATA_DIR);
    diag::FileStatus fs = diag::fileStatus();

    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    time_t now = tv.tv_sec;
    struct tm utc{};
    gmtime_r(&now, &utc);
    char clock[64];
    strftime(clock, sizeof(clock), "%Y-%m-%d %H:%M:%S UTC", &utc);

    LOG_I("app", "PS4 IPTV Playback Test v%s starting (title %s, build %s, git %s, %s)", APP_VERSION, APP_TITLE_ID,
          BUILD_DATE, BUILD_GIT_HASH, BUILD_TYPE);
    LOG_I("app", "console clock: %s", clock);
    if (fs.ok) {
        LOG_I("storage", "log file %s: %s", fs.path.c_str(), fs.detail.c_str());
    } else {
        // still recorded in memory and in the kernel debug output; shown on screen
        LOG_E("storage", "log file %s NOT writable: %s", fs.path.c_str(), fs.detail.c_str());
    }

    SDL_LogSetOutputFunction(sdlLogOutput, nullptr);
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);

    // pPlay main(): network module before anything uses sockets
    uint32_t net = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if (net == 0) {
        LOG_I("platform", "sceSysmoduleLoadModuleInternal(NET) = 0x%08x", net);
    } else {
        LOG_E("platform", "sceSysmoduleLoadModuleInternal(NET) FAILED = 0x%08x", net);
    }

    auto *app = new App({C2D_SCREEN_WIDTH, C2D_SCREEN_HEIGHT});
    while (app->isRunning()) {
        app->flip();
    }
    delete app;

    LOG_I("app", "shutdown complete, calling sceSystemServiceLoadExec(\"exit\")");
    diag::shutdown();

    sceSystemServiceLoadExec("exit", nullptr);
    while (true) {}

    return 0;
}
