// Console facts for the log and Settings > Diagnostics (no user data), and the system notification used when the app
// cannot open its own window. Host builds return fixed placeholder values.

#ifndef PS4IPTV_PLATFORM_SYSTEM_INFO_H
#define PS4IPTV_PLATFORM_SYSTEM_INFO_H

#include <cstdint>
#include <string>

namespace sysinfo {

    // "13.52" from the kernel's version word 0x13520001 (BCD-like: major byte, minor byte); "" when unknown
    std::string formatFirmware(uint32_t version);

    // the system software version ("13.52"), "unknown" when the kernel does not tell
    std::string firmwareVersion();

    // which file system view the process has (booleans only, for the Local Media / startup diagnostics):
    // "/data yes, /mnt yes, /mnt/sandbox no, /system/common/lib no, sandbox word dir yes"
    std::string fileSystemView();

    // a PS4 system notification (top-left pop-up); works without the app's renderer. Host: logged only.
    void notify(const std::string &text);
}

#endif // PS4IPTV_PLATFORM_SYSTEM_INFO_H
