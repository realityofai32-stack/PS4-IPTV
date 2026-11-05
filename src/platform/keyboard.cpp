#include <cstring>

#include "keyboard.h"
#include "log.h"

#ifdef __PS4__

#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/_types/keyboard.h>

namespace {
    const double OPEN_RETRY = 3.0;   // seconds between attempts to open a keyboard that is not connected

    // handle of a loaded system module by file name (the way the SDL2 port finds Piglet)
    int moduleHandle(const char *name) {
        OrbisKernelModule handles[256];
        size_t count = 0;
        if (sceKernelGetModuleList(handles, sizeof(handles) / sizeof(handles[0]), &count) != 0) {
            return -1;
        }
        for (size_t i = 0; i < count; i++) {
            OrbisKernelModuleInfo info;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (sceKernelGetModuleInfo(handles[i], &info) == 0 && strstr(info.name, name) != nullptr) {
                return (int) handles[i];
            }
        }
        return -1;
    }

    template<typename F>
    bool resolve(int module, const char *symbol, F &out) {
        void *addr = nullptr;
        if (sceKernelDlsym(module, symbol, &addr) != 0 || addr == nullptr) {
            LOG_W("keyboard", "%s not found in libSceKeyboard", symbol);
            return false;
        }
        out = (F) addr;
        return true;
    }
}

bool PhysicalKeyboard::init() {
    // the public module id (as in the OpenOrbis keyboard sample), else the internal one
    int rc = sceSysmoduleLoadModule(ORBIS_SYSMODULE_KEYBOARD);
    if (rc != 0) {
        int internal = (int) sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_KEYBOARD);
        if (internal != 0) {
            LOG_W("keyboard", "keyboard module not loaded (0x%08x / 0x%08x): controller input only", (unsigned) rc,
                  (unsigned) internal);
            return false;
        }
    }
    int module = moduleHandle("libSceKeyboard");
    if (module < 0) {
        LOG_W("keyboard", "libSceKeyboard.sprx not found among the loaded modules: controller input only");
        return false;
    }
    if (!resolve(module, "sceKeyboardInit", fnInit) || !resolve(module, "sceKeyboardOpen", fnOpen)
        || !resolve(module, "sceKeyboardClose", fnClose) || !resolve(module, "sceKeyboardReadState", fnReadState)) {
        return false;
    }
    rc = fnInit();
    if (rc != 0) {
        LOG_W("keyboard", "sceKeyboardInit failed (0x%08x): controller input only", (unsigned) rc);
        return false;
    }
    int32_t user = -1;
    if (sceUserServiceGetInitialUser(&user) != 0) {
        LOG_W("keyboard", "no initial user: controller input only");
        return false;
    }
    userId = user;
    available = true;
    LOG_I("keyboard", "keyboard library ready (layout %s)",
          decoder.currentLayout() == kbd::Layout::TurkishQ ? "Turkish Q" : "US");
    return true;
}

void PhysicalKeyboard::tryOpen(double now) {
    nextOpen = now + OPEN_RETRY;
    int h = fnOpen(userId, 0, 0, nullptr);
    if (h >= 0) {
        handle = h;
        readFailures = 0;
        decoder.reset();
        LOG_I("keyboard", "physical keyboard opened");
    }
}

const std::vector<kbd::Event> &PhysicalKeyboard::poll(double now) {
    events.clear();
    if (!available) {
        return events;
    }
    if (handle < 0) {
        if (now >= nextOpen) {
            tryOpen(now);
        }
        if (handle < 0) {
            return events;
        }
    }
    // The state is read into a larger zeroed buffer: OrbisKeyboardData is a reverse-engineered layout (OpenOrbis
    // keyboard sample) and the firmware's own structure must never write past our memory if it is bigger.
    alignas(8) unsigned char raw[512];
    memset(raw, 0, sizeof(raw));
    int rc = fnReadState(handle, raw);
    OrbisKeyboardData data;
    memcpy(&data, raw, sizeof(data));
    if (rc < 0) {
        // unplugged: close and look for a keyboard again later
        if (++readFailures >= 30) {
            LOG_I("keyboard", "physical keyboard disconnected (0x%08x)", (unsigned) rc);
            fnClose(handle);
            handle = -1;
            decoder.reset();
            nextOpen = now + OPEN_RETRY;
        }
        return events;
    }
    readFailures = 0;
    int n = data.nkeys < 0 ? 0 : data.nkeys > 32 ? 32 : data.nkeys;
    events = decoder.update(data.keycodes, n, data.mods, data.locks, now);
    if (!events.empty() && !firstKeyLogged) {
        firstKeyLogged = true;   // never which key: it could be part of a password
        LOG_I("keyboard", "first key press received");
    }
    return events;
}

#else

bool PhysicalKeyboard::init() {
    return false;
}

void PhysicalKeyboard::tryOpen(double) {}

const std::vector<kbd::Event> &PhysicalKeyboard::poll(double) {
    events.clear();
    return events;
}

#endif
