#include <cstdio>
#include <cstring>

#include "system_info.h"
#include "fs.h"
#include "log.h"

#ifdef __PS4__
#include <orbis/libkernel.h>
#endif

namespace sysinfo {

    std::string formatFirmware(uint32_t version) {
        unsigned major = (version >> 24) & 0xff;
        unsigned minor = (version >> 16) & 0xff;
        if (version == 0 || major == 0 || major > 0x99 || minor > 0x99) {
            return "";
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "%x.%02x", major, minor);
        return buf;
    }

#ifdef __PS4__

    std::string firmwareVersion() {
        // a buffer larger than OpenOrbis' 0x28-byte structure: a firmware writing more never writes past it
        alignas(8) unsigned char buf[256];
        memset(buf, 0, sizeof(buf));
        auto *v = reinterpret_cast<OrbisKernelSwVersion *>(buf);
        v->Size = sizeof(OrbisKernelSwVersion);
        if (sceKernelGetSystemSwVersion(v) != 0) {
            return "unknown";
        }
        std::string s = formatFirmware(v->Version);
        return s.empty() ? "unknown" : s;
    }

    std::string fileSystemView() {
        auto yn = [](bool b) { return b ? "yes" : "no"; };
        const char *word = sceKernelGetFsSandboxRandomWord();
        bool wordDir = word != nullptr && word[0] != '\0' && strlen(word) < 64 && fs::isDir(std::string("/") + word);
        char buf[200];
        snprintf(buf, sizeof(buf), "/data %s, /mnt %s, /mnt/sandbox %s, /system/common/lib %s, sandbox word dir %s",
                 yn(fs::isDir("/data")), yn(fs::isDir("/mnt")), yn(fs::isDir("/mnt/sandbox")),
                 yn(fs::isDir("/system/common/lib")), yn(wordDir));
        return buf;
    }

    void notify(const std::string &text) {
        static OrbisNotificationRequest req;   // 3 KiB: not on the stack
        memset(&req, 0, sizeof(req));
        req.type = NotificationRequest;
        req.targetId = -1;
        req.useIconImageUri = 1;
        strncpy(req.iconUri, "cxml://psnotification/tex_icon_system", sizeof(req.iconUri) - 1);
        strncpy(req.message, text.c_str(), sizeof(req.message) - 1);
        int rc = sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
        LOG_I("platform", "system notification sent: 0x%08x", (unsigned) rc);
    }

#else

    std::string firmwareVersion() {
        return "host";
    }

    std::string fileSystemView() {
        return "host";
    }

    void notify(const std::string &text) {
        LOG_I("platform", "system notification (host): %s", text.c_str());
    }

#endif
}
