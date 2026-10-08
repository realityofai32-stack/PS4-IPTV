// A download's .part file: 64-bit offsets, explicit truncation and flush to disk (PS4 and the Windows host).
//
// PS4: musl routes open / lseek / ftruncate / fsync / write to libkernel (FreeBSD: off_t is 64-bit, the
// O_* values in the sysroot's fcntl.h are FreeBSD's). Windows: _lseeki64 / _chsize_s / _commit.

#ifndef PS4IPTV_DOWNLOADS_PART_FILE_H
#define PS4IPTV_DOWNLOADS_PART_FILE_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace dl {

    class PartFile {
    public:
        PartFile() = default;

        ~PartFile();

        PartFile(const PartFile &) = delete;

        PartFile &operator=(const PartFile &) = delete;

        // opens (creating it when missing) for reading and writing; never truncates by itself
        bool open(const std::string &path, std::string *error);

        bool isOpen() const { return fd >= 0; }

        int64_t size();

        // truncates (or extends) to `bytes` and positions the next write there
        bool truncateTo(int64_t bytes, std::string *error);

        // appends at the current position; errnum receives errno on failure (ENOSPC, EFBIG ...)
        bool write(const char *data, size_t n, int *errnum);

        // flushes the written data to the disk
        bool sync();

        void close();

    private:
        int fd = -1;
    };

    // free bytes on the file system holding `path` (-1 when unknown). PS4: fstatfs() on the opened directory
    // (FreeBSD struct statfs: f_bavail * f_bsize); musl's statvfs() does not fill its result on the PS4.
    int64_t freeSpace(const std::string &path);

    // 64-bit file size probe: true when a file can be extended past 4 GiB on this file system. Creates and
    // removes a sparse file (ftruncate, no data written).
    bool supportsLargeFiles(const std::string &dir, std::string *detail);

    // renames, replacing `to` (same file system)
    bool moveFile(const std::string &from, const std::string &to);
}

#endif // PS4IPTV_DOWNLOADS_PART_FILE_H
