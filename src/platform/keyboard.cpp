#include <cstring>

#include "keyboard.h"
#include "log.h"

#ifdef __PS4__

#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/_types/keyboard.h>

#include <cstddef>

// the offsets kbd::parseState decodes are the OpenOrbis structure's
static_assert(offsetof(OrbisKeyboardData, nkeys) == kbd::STATE_NKEYS, "OrbisKeyboardData.nkeys");
static_assert(offsetof(OrbisKeyboardData, locks) == kbd::STATE_LOCKS, "OrbisKeyboardData.locks");
static_assert(offsetof(OrbisKeyboardData, mods) == kbd::STATE_MODS, "OrbisKeyboardData.mods");
static_assert(offsetof(OrbisKeyboardData, keycodes) == kbd::STATE_KEYS, "OrbisKeyboardData.keycodes");
static_assert(sizeof(OrbisKeyboardData) == kbd::STATE_SIZE, "OrbisKeyboardData size");
static_assert(sizeof(OrbisKeyboardData) <= kbd::Session::BUFFER, "state buffer");

namespace {

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
    kbd::Session::Device dev;
    dev.open = [this]() { return fnOpen(userId, 0, 0, nullptr); };
    // the firmware writes the state into the session's zeroed kbd::Session::BUFFER bytes (bigger than the
    // reverse-engineered structure: a larger firmware structure never writes past our memory)
    dev.read = [this](int handle, unsigned char *buffer, size_t) { return fnReadState(handle, buffer); };
    dev.close = [this](int handle) { fnClose(handle); };
    session.reset(new kbd::Session(dev));
    session->decoder().setLayout(layout);
    LOG_I("keyboard", "keyboard library ready (layout %s)", layout == kbd::Layout::TurkishQ ? "Turkish Q" : "US");
    return true;
}

const std::vector<kbd::Event> &PhysicalKeyboard::poll(double now) {
    if (!session) {
        return none;
    }
    const std::vector<kbd::Event> &events = session->poll(now);
    if (session->change() == kbd::Session::Change::Opened) {
        LOG_I("keyboard", "physical keyboard opened");
    } else if (session->change() == kbd::Session::Change::Disconnected) {
        LOG_I("keyboard", "physical keyboard disconnected (0x%08x)", (unsigned) session->lastError());
    }
    if (!events.empty() && !firstKeyLogged) {
        firstKeyLogged = true;   // never which key: it could be part of a password
        LOG_I("keyboard", "first key press received");
    }
    return events;
}

void PhysicalKeyboard::setLayout(kbd::Layout l) {
    layout = l;
    if (session) {
        session->decoder().setLayout(l);
    }
}

#else

bool PhysicalKeyboard::init() {
    return false;
}

const std::vector<kbd::Event> &PhysicalKeyboard::poll(double) {
    return none;
}

void PhysicalKeyboard::setLayout(kbd::Layout l) {
    layout = l;
}

#endif
