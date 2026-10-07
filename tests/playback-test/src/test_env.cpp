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

TestEnv probeTestEnv(const std::string &dataDir) {
    TestEnv env;

    DIR *dir = opendir("/mnt");
    if (dir != nullptr) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                env.mntEntries.emplace_back(ent->d_name);
            }
        }
        closedir(dir);
        std::sort(env.mntEntries.begin(), env.mntEntries.end());
    } else {
        env.mntError = "opendir(/mnt) failed: errno " + std::to_string(errno) + " (" + strerror(errno) + ")";
    }

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

    for (const auto &root: roots) {
        std::string path = root + CONFIG_FILE;
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
