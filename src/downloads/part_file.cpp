#include <cerrno>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __PS4__
#include <sys/statfs.h>
#else
#include <sys/statvfs.h>
#endif
#endif

#include "part_file.h"

namespace dl {

    namespace {
        std::string errnoText(const char *op) {
            int e = errno;
            return std::string(op) + " failed: errno " + std::to_string(e) + " (" + strerror(e) + ")";
        }
    }

    PartFile::~PartFile() {
        close();
    }

    bool PartFile::open(const std::string &path, std::string *error) {
        close();
#ifdef _WIN32
        int h = -1;
        if (_sopen_s(&h, path.c_str(), _O_RDWR | _O_CREAT | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0) {
            h = -1;
        }
        fd = h;
#else
        fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0666);
#endif
        if (fd < 0 && error) {
            *error = errnoText("open");
        }
        return fd >= 0;
    }

    int64_t PartFile::size() {
        if (fd < 0) {
            return -1;
        }
#ifdef _WIN32
        struct _stat64 st{};
        return _fstat64(fd, &st) == 0 ? (int64_t) st.st_size : -1;
#else
        // lseek (a libkernel import pPlay already uses) instead of fstat; the next write position is always set
        // by truncateTo() before data is written
        off_t end = lseek(fd, 0, SEEK_END);
        return end < 0 ? -1 : (int64_t) end;
#endif
    }

    bool PartFile::truncateTo(int64_t bytes, std::string *error) {
        if (fd < 0 || bytes < 0) {
            return false;
        }
#ifdef _WIN32
        bool ok = _chsize_s(fd, bytes) == 0 && _lseeki64(fd, bytes, SEEK_SET) == bytes;
#else
        bool ok = ftruncate(fd, (off_t) bytes) == 0 && lseek(fd, (off_t) bytes, SEEK_SET) == (off_t) bytes;
#endif
        if (!ok && error) {
            *error = errnoText("truncate");
        }
        return ok;
    }

    bool PartFile::write(const char *data, size_t n, int *errnum) {
        while (n > 0) {
#ifdef _WIN32
            int chunk = (int) (n > (1u << 30) ? (1u << 30) : n);
            int w = _write(fd, data, (unsigned) chunk);
#else
            ssize_t w = ::write(fd, data, n);
#endif
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errnum) {
                    *errnum = errno;
                }
                return false;
            }
            if (w == 0) {
                if (errnum) {
                    *errnum = EIO;
                }
                return false;
            }
            data += w;
            n -= (size_t) w;
        }
        return true;
    }

    bool PartFile::sync() {
        if (fd < 0) {
            return false;
        }
#ifdef _WIN32
        return _commit(fd) == 0;
#else
        return fsync(fd) == 0;
#endif
    }

    void PartFile::close() {
        if (fd >= 0) {
#ifdef _WIN32
            _close(fd);
#else
            ::close(fd);
#endif
            fd = -1;
        }
    }

    int64_t freeSpace(const std::string &path) {
#ifdef _WIN32
        ULARGE_INTEGER avail;
        if (GetDiskFreeSpaceExA(path.c_str(), &avail, nullptr, nullptr)) {
            return (int64_t) avail.QuadPart;
        }
        return -1;
#elif defined(__PS4__)
        int dir = ::open(path.c_str(), O_RDONLY);
        if (dir < 0) {
            return -1;
        }
        struct statfs st;
        memset(&st, 0, sizeof(st));
        int rc = fstatfs(dir, &st);
        ::close(dir);
        if (rc != 0 || st.f_bsize == 0) {
            return -1;
        }
        int64_t blocks = st.f_bavail > 0 ? st.f_bavail : 0;
        return blocks * (int64_t) st.f_bsize;
#else
        struct statvfs st{};
        if (statvfs(path.c_str(), &st) != 0) {
            return -1;
        }
        return (int64_t) st.f_bavail * (int64_t) st.f_frsize;
#endif
    }

    bool supportsLargeFiles(const std::string &dir, std::string *detail) {
        const int64_t probeSize = (4ll << 30) + 4096;   // past 4 GiB
        std::string path = dir + (dir.empty() || dir.back() == '/' ? "" : "/") + ".largefile-probe";
        PartFile f;
        std::string err;
        if (!f.open(path, &err)) {
            if (detail) {
                *detail = err;
            }
            return false;
        }
        bool ok = f.truncateTo(probeSize, &err) && f.size() == probeSize;
        f.close();
        remove(path.c_str());
        if (detail) {
            *detail = ok ? "files larger than 4 GiB supported" : (err.empty() ? "size check failed" : err);
        }
        return ok;
    }

    bool moveFile(const std::string &from, const std::string &to) {
#ifdef _WIN32
        return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        return rename(from.c_str(), to.c_str()) == 0;
#endif
    }
}
