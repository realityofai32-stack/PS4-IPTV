#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include "fs.h"

namespace fs {

    namespace {
        std::string errnoText(const char *op, const std::string &path) {
            int e = errno;
            return std::string(op) + "(" + shownPath(path) + ") failed: errno " + std::to_string(e) + " (" + strerror(e)
                   + ")";
        }

        int makeDir(const std::string &path) {
#ifdef _WIN32
            return _mkdir(path.c_str());
#else
            return mkdir(path.c_str(), 0777);
#endif
        }

        bool flushToDisk(FILE *f) {
            if (fflush(f) != 0) {
                return false;
            }
#ifdef _WIN32
            return _commit(_fileno(f)) == 0;
#else
            return fsync(fileno(f)) == 0;
#endif
        }

        bool replaceFile(const std::string &from, const std::string &to) {
#ifdef _WIN32
            return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            return rename(from.c_str(), to.c_str()) == 0;
#endif
        }
    }

    std::string shownPath(const std::string &path) {
        auto rootOf = [&path](const std::string &prefix, bool digit) -> size_t {
            if (path.compare(0, prefix.size(), prefix) != 0) {
                return std::string::npos;
            }
            size_t end = prefix.size();
            if (digit) {
                if (end >= path.size() || path[end] < '0' || path[end] > '9') {
                    return std::string::npos;
                }
                end++;
            }
            return end == path.size() || path[end] == '/' ? end : std::string::npos;
        };
        size_t root = std::string::npos;
        for (const auto &r: {std::make_pair("/usb", true), std::make_pair("/mnt/usb", true), std::make_pair("/disc", false),
                             std::make_pair("/mnt/disc", false)}) {
            if ((root = rootOf(r.first, r.second)) != std::string::npos) {
                break;
            }
        }
        if (root == std::string::npos) {
            size_t pl = path.find("/playlists/");
            if (pl == std::string::npos) {
                return path;
            }
            root = pl + 10;   // ".../playlists"
        }
        return root + 1 >= path.size() ? path : path.substr(0, root) + "/[...]";
    }

    std::string join(const std::string &dir, const std::string &name) {
        if (dir.empty()) {
            return name;
        }
        char last = dir.back();
        return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
    }

    bool exists(const std::string &path) {
        struct stat st{};
        return stat(path.c_str(), &st) == 0;
    }

    bool isDir(const std::string &path) {
        struct stat st{};
        return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
    }

    int64_t fileSize(const std::string &path) {
#ifdef _WIN32
        struct _stat64 st{};   // MSVC's plain stat has a 32-bit st_size; downloads exceed 4 GB
        return _stat64(path.c_str(), &st) == 0 ? (int64_t) st.st_size : -1;
#else
        struct stat st{};      // PS4 (FreeBSD libkernel): off_t st_size is 64-bit
        return stat(path.c_str(), &st) == 0 ? (int64_t) st.st_size : -1;
#endif
    }

    bool ensureDir(const std::string &path, std::string *error) {
        if (path.empty() || isDir(path)) {
            return true;
        }
        std::string p = path;
        while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
            p.pop_back();
        }
        size_t slash = p.find_last_of("/\\");
        if (slash != std::string::npos && slash > 0) {
            std::string parent = p.substr(0, slash);
#ifdef _WIN32
            if (!(parent.size() == 2 && parent[1] == ':'))
#endif
            if (!ensureDir(parent, error)) {
                return false;
            }
        }
        if (makeDir(p) != 0 && errno != EEXIST) {
            if (error) {
                *error = errnoText("mkdir", p);
            }
            return false;
        }
        return true;
    }

    bool readFile(const std::string &path, std::string &out, size_t maxBytes, std::string *error) {
        out.clear();
        if (isDir(path)) {
            // fopen() opens a directory on some systems and fread() then fails differently on each: decided here
            if (error) {
                *error = path + " is a directory";
            }
            return false;
        }
        FILE *f = fopen(path.c_str(), "rb");
        if (f == nullptr) {
            if (error) {
                *error = errnoText("fopen", path);
            }
            return false;
        }
        char buf[64 * 1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            if (out.size() + n > maxBytes) {
                fclose(f);
                out.clear();
                if (error) {
                    *error = path + " is larger than " + std::to_string(maxBytes) + " bytes";
                }
                return false;
            }
            out.append(buf, n);
        }
        bool ok = !ferror(f);
        fclose(f);
        if (!ok && error) {
            *error = errnoText("fread", path);
        }
        return ok;
    }

    bool writeFileAtomic(const std::string &path, const std::string &data, std::string *error) {
        if (isDir(path)) {
            if (error) {
                *error = path + " is a directory";
            }
            return false;
        }
        std::string tmp = path + ".tmp";
        FILE *f = fopen(tmp.c_str(), "wb");
        if (f == nullptr) {
            if (error) {
                *error = errnoText("fopen", tmp);
            }
            return false;
        }
        bool ok = fwrite(data.data(), 1, data.size(), f) == data.size() && flushToDisk(f);
        if (fclose(f) != 0) {
            ok = false;
        }
        if (!ok) {
            if (error) {
                *error = errnoText("write", tmp);
            }
            remove(tmp.c_str());
            return false;
        }
        if (exists(path) && !isDir(path + ".bak")) {
            std::string bak = path + ".bak";
            if (!replaceFile(path, bak)) {
                // not fatal: the final rename still replaces the file atomically on POSIX
                remove(bak.c_str());
            }
        }
        if (!replaceFile(tmp, path)) {
            if (error) {
                *error = errnoText("rename", tmp);
            }
            return false;
        }
        return true;
    }

    bool writeFileReplace(const std::string &path, const std::string &data, std::string *error) {
        if (isDir(path)) {
            if (error) {
                *error = path + " is a directory";
            }
            return false;
        }
        std::string tmp = path + ".tmp";
        FILE *f = fopen(tmp.c_str(), "wb");
        if (f == nullptr) {
            if (error) {
                *error = errnoText("fopen", tmp);
            }
            return false;
        }
        bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
        if (fclose(f) != 0) {
            ok = false;
        }
        if (!ok || !replaceFile(tmp, path)) {
            if (error) {
                *error = errnoText(ok ? "rename" : "write", tmp);
            }
            remove(tmp.c_str());
            return false;
        }
        return true;
    }

    bool removeFile(const std::string &path) {
        return remove(path.c_str()) == 0;
    }

    namespace {
        // byte-for-byte equality of two files, streamed (no size limit, no large allocation)
        bool sameContent(const std::string &a, const std::string &b) {
            if (fileSize(a) != fileSize(b)) {
                return false;
            }
            FILE *fa = fopen(a.c_str(), "rb");
            FILE *fb = fopen(b.c_str(), "rb");
            bool same = fa != nullptr && fb != nullptr;
            char ba[16 * 1024], bb[16 * 1024];
            while (same) {
                size_t na = fread(ba, 1, sizeof(ba), fa);
                size_t nb = fread(bb, 1, sizeof(bb), fb);
                if (na != nb || memcmp(ba, bb, na) != 0) {
                    same = false;
                } else if (na == 0) {
                    break;
                }
            }
            if (fa) {
                fclose(fa);
            }
            if (fb) {
                fclose(fb);
            }
            return same;
        }

        bool copyFile(const std::string &from, const std::string &to) {
            FILE *in = fopen(from.c_str(), "rb");
            if (in == nullptr) {
                return false;
            }
            std::string tmp = to + ".tmp";
            FILE *out = fopen(tmp.c_str(), "wb");
            if (out == nullptr) {
                fclose(in);
                return false;
            }
            char buf[64 * 1024];
            size_t n;
            bool ok = true;
            while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0) {
                ok = fwrite(buf, 1, n, out) == n;
            }
            ok = ok && !ferror(in) && flushToDisk(out);
            fclose(in);
            if (fclose(out) != 0) {
                ok = false;
            }
            if (!ok || !replaceFile(tmp, to)) {
                remove(tmp.c_str());
                return false;
            }
            return true;
        }
    }

    std::string keepDamagedCopy(const std::string &path) {
        if (!exists(path) || isDir(path) || fileSize(path) <= 0) {
            return "";   // nothing there, or nothing in it to keep
        }
        const std::string names[3] = {path + ".damaged", path + ".damaged-2", path + ".damaged-3"};
        for (const std::string &name: names) {
            if (exists(name) && sameContent(path, name)) {
                return name;   // this very content is already kept
            }
        }
        for (const std::string &name: names) {
            if (!exists(name)) {
                return copyFile(path, name) ? name : "";
            }
        }
        return "";   // three different damaged versions are kept already: the oldest stay
    }

    std::vector<Entry> listDir(const std::string &path) {
        std::vector<Entry> out;
#ifdef _WIN32
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(join(path, "*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            return out;
        }
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
                continue;
            }
            Entry e;
            e.name = fd.cFileName;
            e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e.size = ((int64_t) fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            out.push_back(e);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
#else
        DIR *d = opendir(path.c_str());
        if (d == nullptr) {
            return out;
        }
        struct dirent *ent;
        while ((ent = readdir(d)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            Entry e;
            e.name = ent->d_name;
            struct stat st{};
            if (stat(join(path, e.name).c_str(), &st) == 0) {
                e.dir = (st.st_mode & S_IFMT) == S_IFDIR;
                e.size = (int64_t) st.st_size;
                e.mtime = (int64_t) st.st_mtime;
            }
            out.push_back(e);
        }
        closedir(d);
#endif
        return out;
    }
}
