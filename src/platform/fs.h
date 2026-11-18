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
    // A directory where the file belongs is never moved or removed: the write fails.
    bool writeFileAtomic(const std::string &path, const std::string &data, std::string *error = nullptr);

    // Cache files: writes <path>.tmp and renames it into place (no backup copy, no flush to disk). A crash
    // leaves the old file, the new file or a stray .tmp - never a partial file under `path`.
    bool writeFileReplace(const std::string &path, const std::string &data, std::string *error = nullptr);

    bool removeFile(const std::string &path);

    // A data file the app could not read (damaged, truncated, from a newer version...): a copy of it is kept beside it
    // as <path>.damaged (then .damaged-2, .damaged-3) BEFORE anything can overwrite it, so user data is never lost
    // only because it could not be parsed. Nothing is copied when there is no such file, when it is empty or a
    // directory, when an identical copy is already kept, or when all three names are taken. Returns the kept copy's path, "" when
    // none was made.
    std::string keepDamagedCopy(const std::string &path);

    struct Entry {
        std::string name;
        bool dir = false;
        int64_t size = 0;
        int64_t mtime = 0;
    };

    std::vector<Entry> listDir(const std::string &path);

    std::string join(const std::string &dir, const std::string &name);

    // a path as error texts (and so the log) show it: below a USB drive (/usbN, /mnt/usbN), a disc (/disc,
    // /mnt/disc) or the user's playlist folder (.../playlists/) only that root is kept - folder and file names there
    // are the user's ("/usb0/[...]"); the app's own data paths are shown as they are
    std::string shownPath(const std::string &path);
}

#endif // PS4IPTV_PLATFORM_FS_H
