#include <algorithm>
#include <cctype>
#include <map>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "local_media.h"
#include "../platform/fs.h"

namespace localmedia {

    std::vector<Root> candidateRoots() {
        std::vector<Root> roots;
        roots.push_back({"/data/", false, 0});
        for (int i = 0; i < 8; i++) {
            roots.push_back({"/mnt/usb" + std::to_string(i) + "/", true, i + 1});
        }
        return roots;
    }

    bool normalize(const std::string &path, std::string &out) {
        if (path.empty() || path[0] != '/' || path.find('\0') != std::string::npos) {
            return false;
        }
        std::vector<std::string> parts;
        size_t start = 1;
        while (start <= path.size()) {
            size_t slash = path.find('/', start);
            std::string seg = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (seg == "..") {
                return false;   // never climbs, even when it would stay inside
            }
            if (!seg.empty() && seg != ".") {
                parts.push_back(seg);
            }
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
        out.clear();
        for (const auto &p: parts) {
            out += "/" + p;
        }
        if (out.empty()) {
            out = "/";
        }
        return true;
    }

    bool within(const Root &root, const std::string &path) {
        std::string r, p;
        if (!normalize(root.path, r) || !normalize(path, p)) {
            return false;
        }
        return p == r || (p.size() > r.size() && p.compare(0, r.size(), r) == 0 && p[r.size()] == '/');
    }

    const Root *rootOf(const std::vector<Root> &roots, const std::string &path) {
        for (const auto &r: roots) {
            if (within(r, path)) {
                return &r;
            }
        }
        return nullptr;
    }

    bool safeName(const std::string &name) {
        return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos
               && name.find('\0') == std::string::npos;
    }

    std::string extensionOf(const std::string &name) {
        size_t dot = name.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size()) {
            return "";
        }
        std::string ext = name.substr(dot + 1);
        for (char &c: ext) {
            c = (char) std::tolower((unsigned char) c);
        }
        return ext;
    }

    const std::set<std::string> &Formats::excludedDemuxers() {
        // subtitles, images, text art, playlists / manifests, metadata and generated signals: FFmpeg can read
        // them, but none is a video or audio file to play from a folder
        static const std::set<std::string> excluded = {
                "aqtitle", "ass", "jacosub", "lrc", "microdvd", "mpl2", "mpsub", "pjs", "realtext", "sami", "scc",
                "srt", "stl", "subviewer", "subviewer1", "vplayer", "webvtt", "vobsub", "sup", "tedcaptions",
                "dvbsub", "dvbtxt",
                "image2", "image2pipe", "gif", "apng", "ico", "alias_pix", "brender_pix", "fits", "bitpacked",
                "tty", "bintext", "adf", "idf", "xbin",
                "hls", "dash", "concat", "ffmetadata", "sbg", "data", "lavfi", "loas_text"};
        return excluded;
    }

    const std::map<std::string, std::string> &Formats::probedExtensions() {
        // demuxers FFmpeg 5.0 finds by probing the content: no extension list of their own. Their usual file names,
        // used only when the demuxer is in the build.
        static const std::map<std::string, std::string> probed = {
                {"mpegts", "ts,m2t,m2ts,mts,tp,trp"}, {"mpeg", "mpg,mpeg,vob,m2p"}, {"asf", "asf,wmv,wma"},
                {"wav", "wav"}, {"w64", "w64"}, {"rm", "rm,rmvb,ra"}, {"ogg", "oga,ogv,opus,ogm"}};
        return probed;
    }

    void Formats::addDemuxer(const std::string &name, const std::string &listed) {
        std::string extensions = listed;
        auto probed = probedExtensions().find(name);
        if (probed != probedExtensions().end()) {
            extensions += (extensions.empty() ? "" : ",") + probed->second;
        }
        if (extensions.empty()) {
            return;
        }
        // a demuxer name may list aliases ("matroska,webm"); image demuxers are "<format>_pipe"
        size_t start = 0;
        while (start <= name.size()) {
            size_t comma = name.find(',', start);
            std::string n = name.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (excludedDemuxers().count(n) || (n.size() > 5 && n.compare(n.size() - 5, 5, "_pipe") == 0)) {
                return;
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        start = 0;
        while (start <= extensions.size()) {
            size_t comma = extensions.find(',', start);
            std::string e = extensions.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            for (char &c: e) {
                c = (char) std::tolower((unsigned char) c);
            }
            e.erase(std::remove_if(e.begin(), e.end(), [](char c) { return std::isspace((unsigned char) c); }), e.end());
            if (!e.empty()) {
                exts.insert(e);
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
    }

    bool Formats::supported(const std::string &extension) const {
        std::string e = extension;
        for (char &c: e) {
            c = (char) std::tolower((unsigned char) c);
        }
        return !e.empty() && exts.count(e) > 0;
    }

    bool naturalLess(const std::string &a, const std::string &b) {
        size_t i = 0, j = 0;
        while (i < a.size() && j < b.size()) {
            unsigned char ca = (unsigned char) a[i];
            unsigned char cb = (unsigned char) b[j];
            if (std::isdigit(ca) && std::isdigit(cb)) {
                size_t ei = i, ej = j;
                while (ei < a.size() && std::isdigit((unsigned char) a[ei])) {
                    ei++;
                }
                while (ej < b.size() && std::isdigit((unsigned char) b[ej])) {
                    ej++;
                }
                // compare the numbers without leading zeros: by length, then digit by digit
                size_t si = i, sj = j;
                while (si + 1 < ei && a[si] == '0') {
                    si++;
                }
                while (sj + 1 < ej && b[sj] == '0') {
                    sj++;
                }
                if (ei - si != ej - sj) {
                    return ei - si < ej - sj;
                }
                int c = a.compare(si, ei - si, b, sj, ej - sj);
                if (c != 0) {
                    return c < 0;
                }
                i = ei;
                j = ej;
                continue;
            }
            int la = std::tolower(ca);
            int lb = std::tolower(cb);
            if (la != lb) {
                return la < lb;
            }
            i++;
            j++;
        }
        if ((a.size() - i) != (b.size() - j)) {
            return a.size() - i < b.size() - j;
        }
        return a < b;   // equal ignoring case: a stable, deterministic order
    }

    std::vector<Entry> prepare(std::vector<Entry> raw, const Formats &formats) {
        std::vector<Entry> dirs, files;
        for (auto &e: raw) {
            if (!safeName(e.name) || e.name[0] == '.') {
                continue;
            }
            if (e.directory) {
                dirs.push_back(std::move(e));
            } else if (formats.supported(extensionOf(e.name))) {
                files.push_back(std::move(e));
            }
        }
        auto byName = [](const Entry &x, const Entry &y) { return naturalLess(x.name, y.name); };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);
        dirs.insert(dirs.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
        return dirs;
    }

    Listing list(const Root &root, const std::string &folder, const Formats &formats) {
        Listing out;
        std::string dir;
        if (!normalize(folder, dir) || !within(root, dir)) {
            return out;   // outside the root: refused
        }
        std::vector<Entry> raw;
        int total = 0;
#ifndef _WIN32
        DIR *d = opendir(dir.c_str());
        if (d == nullptr) {
            return out;
        }
        while (struct dirent *de = readdir(d)) {
            std::string name = de->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            total++;
            if (!safeName(name) || raw.size() >= 5000) {
                continue;
            }
            Entry e;
            e.name = name;
            e.path = (dir == "/" ? "" : dir) + "/" + name;
            struct stat st;
            if (lstat(e.path.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) {
                continue;   // symbolic links are never followed (they could point outside the root)
            }
            if (S_ISDIR(st.st_mode)) {
                e.directory = true;
            } else if (S_ISREG(st.st_mode)) {
                e.size = (int64_t) st.st_size;
            } else {
                continue;
            }
            raw.push_back(std::move(e));
        }
        closedir(d);
#else
        // host tests on Windows (the console and Linux use the branch above)
        if (!fs::isDir(dir)) {
            return out;
        }
        for (const auto &fe: fs::listDir(dir)) {
            total++;
            if (!safeName(fe.name) || raw.size() >= 5000) {
                continue;
            }
            Entry e;
            e.name = fe.name;
            e.path = dir + "/" + fe.name;
            e.directory = fe.dir;
            e.size = fe.dir ? -1 : fe.size;
            raw.push_back(std::move(e));
        }
#endif
        out.entries = prepare(std::move(raw), formats);
        out.skipped = total - (int) out.entries.size();
        out.ok = true;
        return out;
    }

    bool available(const Root &root) {
        std::string dir;
        if (!normalize(root.path, dir)) {
            return false;
        }
#ifndef _WIN32
        DIR *d = opendir(dir.c_str());
        if (d == nullptr) {
            return false;
        }
        closedir(d);
        return true;
#else
        return fs::isDir(dir);
#endif
    }
}
