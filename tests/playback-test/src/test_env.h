// Discovery of the test inputs on the PS4: USB mounts, local test file and test_streams.txt.
//
// No mount path is assumed: /mnt is listed and /mnt/usb0../mnt/usb7 are probed at runtime, and the
// result is logged and shown on screen.

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
    std::vector<std::string> mntEntries;
    std::string mntError;
    std::vector<UsbProbe> usb;
    StreamConfig config;
    std::string localFile;            // resolved local test file, empty if none found
    std::vector<std::string> localCandidates;
};

// Searches USB roots then dataDir for test.mp4 and test_streams.txt.
TestEnv probeTestEnv(const std::string &dataDir);

// Parses the KEY=value file format (exposed for host-side tests).
StreamConfig parseStreamConfig(const std::string &content, const std::string &path);

#endif // PS4IPTV_TEST_TEST_ENV_H
