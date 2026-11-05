// Physical USB / Bluetooth keyboard on the console (libSceKeyboard), next to the DualShock 4.
//
// The library is loaded at run time (sceSysmoduleLoadModule(ORBIS_SYSMODULE_KEYBOARD), then its functions are
// looked up with sceKernelDlsym): the executable gains no new system module import, and a firmware or setup where
// the module is unavailable simply runs without a physical keyboard. The keyboard of the user who started the app
// is opened; a keyboard connected later is picked up (the open is retried every few seconds). Keys are decoded by
// kbd::Decoder (platform/keyboard_logic.h). Host builds have no keyboard (poll() returns nothing).

#ifndef PS4IPTV_PLATFORM_KEYBOARD_H
#define PS4IPTV_PLATFORM_KEYBOARD_H

#include <vector>

#include "keyboard_logic.h"

class PhysicalKeyboard {

public:

    // loads the module once; false when the console offers no keyboard library (logged)
    bool init();

    void setLayout(kbd::Layout layout) { decoder.setLayout(layout); }

    // reads the keyboard state (call once per frame) and returns the key events since the last call
    const std::vector<kbd::Event> &poll(double now);

    bool connected() const { return handle >= 0; }

private:

    void tryOpen(double now);

    bool available = false;
    int userId = -1;
    int handle = -1;
    double nextOpen = 0;
    int readFailures = 0;
    bool firstKeyLogged = false;
    kbd::Decoder decoder;
    std::vector<kbd::Event> events;

    // resolved from libSceKeyboard
    int (*fnInit)() = nullptr;
    int (*fnOpen)(int, int, int, void *) = nullptr;
    int (*fnClose)(int) = nullptr;
    int (*fnReadState)(int, void *) = nullptr;
};

#endif // PS4IPTV_PLATFORM_KEYBOARD_H
