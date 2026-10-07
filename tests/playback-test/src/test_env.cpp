#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "test_env.h"

namespace {

    const char *const TEST_FILE = "test.mp4";
    const char *const CONFIG_FILE = "test_streams.txt";
    const long MAX_CONFIG_BYTES = 64 * 1024;

    bool isDir(const std::string &path) {
        struct stat st{};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    bool isFile(const std::string &path) {
        struct stat st{};
        return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }

    std::vector<std::string> listDir(const char *path, std::string &error) {
        std::vector<std::string> entries;
        DIR *dir = opendir(path);
        if (dir == nullptr) {
            error = std::string("opendir(") + path + ") failed: errno " + std::to_string(errno) + " ("
                    + strerror(errno) + ")";
            return entries;
        }
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr && entries.size() < 64) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                entries.emplace_back(ent->d_name);
            }
        }
        closedir(dir);
        std::sort(entries.begin(), entries.end());
        return entries;
    }

    bool readFile(const std::string &path, std::string &out, std::string &error) {
        FILE *f = fopen(path.c_str(), "rb");
        if (f == nullptr) {
            error = "fopen failed: errno " + std::to_string(errno) + " (" + strerror(errno) + ")";
            return false;
        }
        char buf[4096];
        size_t n;
        out.clear();
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            out.append(buf, n);
            if ((long) out.size() > MAX_CONFIG_BYTES) {
                fclose(f);
                error = "file larger than 64 KiB";
                return false;
            }
        }
        fclose(f);
        return true;
    }
}

TestEnv probeTestEnv(const std::string &dataDir, const std::string &romfsDir) {
    TestEnv env;

    env.rootEntries = listDir("/", env.rootError);
    env.mntEntries = listDir("/mnt", env.mntError);

    std::vector<std::string> roots;
    for (int i = 0; i < 8; i++) {
        UsbProbe p;
        p.path = "/mnt/usb" + std::to_string(i) + "/";
        p.exists = isDir(p.path);
        if (p.exists) {
            p.hasTestFile = isFile(p.path + TEST_FILE);
            p.hasConfig = isFile(p.path + CONFIG_FILE);
            roots.push_back(p.path);
        }
        env.usb.push_back(p);
    }
    roots.push_back(dataDir);

    // config: packaged in the PKG first (no USB dependency), then the data dir, then USB roots
    std::vector<std::string> configRoots;
    configRoots.push_back(romfsDir);
    configRoots.push_back(dataDir);
    for (const auto &root: roots) {
        if (root != dataDir) {
            configRoots.push_back(root);
        }
    }
    for (const auto &root: configRoots) {
        std::string path = root + CONFIG_FILE;
        env.configCandidates.push_back(path);
        if (isFile(path)) {
            std::string content, error;
            if (readFile(path, content, error)) {
                env.config = parseStreamConfig(content, path);
            } else {
                env.config.found = true;
                env.config.path = path;
                env.config.problems.push_back("read failed: " + error);
            }
            break;
        }
    }

    if (!env.config.localPath.empty()) {
        env.localCandidates.push_back(env.config.localPath);
    }
    for (const auto &root: roots) {
        env.localCandidates.push_back(root + TEST_FILE);
    }
    for (const auto &candidate: env.localCandidates) {
        if (isFile(candidate)) {
            env.localFile = candidate;
            break;
        }
    }

    return env;
}
