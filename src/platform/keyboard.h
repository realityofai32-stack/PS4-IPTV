// Physical USB / Bluetooth keyboard on the console (libSceKeyboard), next to the DualShock 4.
//
// The library is loaded at run time (sceSysmoduleLoadModule(ORBIS_SYSMODULE_KEYBOARD), then its functions are
// looked up with sceKernelDlsym): the executable gains no new system module import, and a firmware or setup where
// the module is unavailable simply runs without a physical keyboard. The keyboard of the user who started the app
// is opened; a keyboard connected later is picked up (the open is retried every few seconds). Connection, the state
// buffer and the keys are handled by kbd::Session (platform/keyboard_logic.h, host-tested); this file only finds
// the library's functions. Host builds have no keyboard (poll() returns nothing).

#ifndef PS4IPTV_PLATFORM_KEYBOARD_H
#define PS4IPTV_PLATFORM_KEYBOARD_H

#include <memory>
#include <vector>

#include "keyboard_logic.h"

class PhysicalKeyboard {

public:

    // loads the module once; false when the console offers no keyboard library (logged)
    bool init();

    void setLayout(kbd::Layout layout);

    // reads the keyboard state (call once per frame) and returns the key events since the last call
    const std::vector<kbd::Event> &poll(double now);

    bool connected() const { return session && session->connected(); }

private:

    kbd::Layout layout = kbd::Layout::Us;
    int userId = -1;
    bool firstKeyLogged = false;
    std::unique_ptr<kbd::Session> session;   // connection, buffer parsing and decoding (host-tested)
    std::vector<kbd::Event> none;

    // resolved from libSceKeyboard
    int (*fnInit)() = nullptr;
    int (*fnOpen)(int, int, int, void *) = nullptr;
    int (*fnClose)(int) = nullptr;
    int (*fnReadState)(int, void *) = nullptr;
};

#endif // PS4IPTV_PLATFORM_KEYBOARD_H
