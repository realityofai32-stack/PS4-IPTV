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
            return std::string(op) + "(" + path + ") failed: errno " + std::to_string(e) + " (" + strerror(e) + ")";
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
        struct stat st{};
        return stat(path.c_str(), &st) == 0 ? (int64_t) st.st_size : -1;
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
        if (exists(path)) {
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

    bool removeFile(const std::string &path) {
        return remove(path.c_str()) == 0;
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
