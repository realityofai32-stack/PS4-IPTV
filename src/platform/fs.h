// File helpers with crash-safe writes. Portable (PS4 and the Windows host tests).

#ifndef PS4IPTV_PLATFORM_FS_H
#define PS4IPTV_PLATFORM_FS_H

#include <cstdint>
#include <string>
#include <vector>

namespace fs {

    // mkdir -p
    bool ensureDir(const std::string &path, std::string *error = nullptr);

    bool exists(const std::string &path);

    bool isDir(const std::string &path);

    int64_t fileSize(const std::string &path);

    // Reads a whole file, failing if it is larger than maxBytes.
    bool readFile(const std::string &path, std::string &out, size_t maxBytes, std::string *error = nullptr);

    // Writes <path>.tmp, flushes it to disk, keeps the previous version as <path>.bak and renames
    // the new file into place. A crash leaves either the old or the new file, never a partial one.
    bool writeFileAtomic(const std::string &path, const std::string &data, std::string *error = nullptr);

    // Cache files: writes <path>.tmp and renames it into place (no backup copy, no flush to disk). A crash
    // leaves the old file, the new file or a stray .tmp - never a partial file under `path`.
    bool writeFileReplace(const std::string &path, const std::string &data, std::string *error = nullptr);

    bool removeFile(const std::string &path);

    struct Entry {
        std::string name;
        bool dir = false;
        int64_t size = 0;
        int64_t mtime = 0;
    };

    std::vector<Entry> listDir(const std::string &path);

    std::string join(const std::string &dir, const std::string &name);
}

#endif // PS4IPTV_PLATFORM_FS_H
