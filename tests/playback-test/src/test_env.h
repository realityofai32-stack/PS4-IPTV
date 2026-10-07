// Discovery of the test inputs on the PS4.
//
// The stream configuration is packaged into the PKG at build time (/app0/test_streams.txt, read
// through libcross2d's romfs path, the same mechanism pPlay uses for its skin), so the network tests
// do not depend on USB. "/" and "/mnt" are listed only as evidence of what this process can see.

#ifndef PS4IPTV_TEST_TEST_ENV_H
#define PS4IPTV_TEST_TEST_ENV_H

#include <string>
#include <utility>
#include <vector>

struct UsbProbe {
    std::string path;
    bool exists = false;
    bool hasTestFile = false;
    bool hasConfig = false;
};

struct StreamConfig {
    bool found = false;
    std::string path;                 // where test_streams.txt was read from
    std::string tsUrl;
    std::string hlsUrl;
    std::string localPath;            // LOCAL= override
    std::string userAgent;            // USER_AGENT= (optional)
    std::vector<std::pair<std::string, std::string>> mpvOptions;  // MPV_OPT=name=value (optional)
    std::vector<std::string> problems;
};

struct TestEnv {
    std::vector<std::string> rootEntries;   // what this process sees at "/" (sandbox evidence)
    std::string rootError;
    std::vector<std::string> mntEntries;
    std::string mntError;
    std::vector<std::string> configCandidates;
    std::vector<UsbProbe> usb;
    StreamConfig config;
    std::string localFile;            // resolved local test file, empty if none found
    std::vector<std::string> localCandidates;
};

// test_streams.txt: romfsDir (packaged in the PKG) -> dataDir -> /mnt/usbN/.
// test.mp4: /mnt/usbN/ -> dataDir. Never blocks: only stat/opendir.
TestEnv probeTestEnv(const std::string &dataDir, const std::string &romfsDir);

// Parses the KEY=value file format (exposed for host-side tests).
StreamConfig parseStreamConfig(const std::string &content, const std::string &path);

#endif // PS4IPTV_TEST_TEST_ENV_H
