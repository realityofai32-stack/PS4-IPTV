// V7 IPTV (title IPTV00002) - entry point.
// Start/exit sequence follows pPlay's main(): network module, renderer, loop, sceSystemServiceLoadExec("exit").

#include <SDL2/SDL.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>

#include "app/app.h"
#include "build_info.h"
#include "platform/log.h"
#include "platform/startup_state.h"
#include "platform/system_info.h"

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
    LOG_I("app", "V7 IPTV v%s starting (title %s, build %s, git %s, %s)", APP_VERSION, APP_TITLE_ID, BUILD_DATE,
          BUILD_GIT_HASH, BUILD_TYPE);
    if (!fs.ok) {
        LOG_E("storage", "log file %s not writable: %s", fs.path.c_str(), fs.detail.c_str());
    }
    // firmware-specific problems are reported per system software version; the file system view tells a sandboxed
    // process (no /mnt/usbN) from one that sees the console's mounts
    LOG_I("platform", "system software %s; file system: %s", sysinfo::firmwareVersion().c_str(),
          sysinfo::fileSystemView().c_str());

    // startup breadcrumbs (<data>/logs/startup.txt) + the crash guard for optional stages (platform/startup_state.h)
    startup::Tracker &boot = startup::tracker();
    boot.begin(APP_DATA_DIR "logs/", std::string(APP_VERSION) + "-" + BUILD_GIT_HASH);

    // SDL's own messages (Piglet / OpenGL ES module load, pads, audio) go to the log from here on
    SDL_LogSetOutputFunction(sdlLogOutput, nullptr);
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_INFO);

    boot.stage(startup::NETWORK_MODULE);
    uint32_t net = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if (net != 0) {
        LOG_E("platform", "sceSysmoduleLoadModuleInternal(NET) failed: 0x%08x", net);
    }

    // libcross2d + SDL2: controller (scePad), system modules, the Piglet OpenGL ES module, window, GL context, shaders;
    // then App's members (stage app-members, App::membersStage)
    boot.stage(startup::RENDERER);
    auto *app = new App();
    if (app->startupFailed()) {
        // no window: nothing of the app is torn down (its GL objects never existed); the log says why
        LOG_E("app", "startup failed: exit");
        diag::shutdown();
        sceSystemServiceLoadExec("exit", nullptr);
        while (true) {}
    }
    app->run();
    boot.stage(startup::SHUTDOWN);
    delete app;
    boot.exited();

    LOG_I("app", "exit");
    diag::shutdown();
    sceSystemServiceLoadExec("exit", nullptr);
    while (true) {}
    return 0;
}
