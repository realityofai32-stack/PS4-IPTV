#include <algorithm>

#include "user_playlists.h"
#include "../iptv/m3u.h"
#include "../platform/fs.h"

namespace userpl {

    namespace {
        bool isSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        std::string lower(std::string s) {
            for (char &c: s) {
                if (c >= 'A' && c <= 'Z') {
                    c = (char) (c - 'A' + 'a');
                }
            }
            return s;
        }

        bool playlistFile(const std::string &name) {
            std::string l = lower(name);
            auto ends = [&l](const char *ext) {
                size_t n = std::char_traits<char>::length(ext);
                return l.size() > n && l.compare(l.size() - n, n, ext) == 0;
            };
            return ends(".m3u") || ends(".m3u8");
        }

        std::string stem(const std::string &file) {
            size_t slash = file.find_last_of('/');
            std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
            size_t dot = name.rfind('.');
            return dot != std::string::npos && dot > 0 ? name.substr(0, dot) : name;
        }

        // an attribute value: no quotes, no control characters
        std::string attr(const std::string &s) {
            std::string out;
            for (char c: s) {
                if ((unsigned char) c < 0x20 || c == 0x7F) {
                    out += ' ';
                } else {
                    out += c == '"' ? '\'' : c;
                }
            }
            return out;
        }

        // #PLAYLIST:<title> (the first one)
        std::string titleOf(std::string_view text) {
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find_first_of("\r\n", pos);
                if (end == std::string_view::npos) {
                    end = text.size();
                }
                std::string_view line = text.substr(pos, end - pos);
                while (!line.empty() && isSpace(line.front())) {
                    line.remove_prefix(1);
                }
                if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
                    line.remove_prefix(3);
                }
                if (line.size() > 10 && lower(std::string(line.substr(0, 10))) == "#playlist:") {
                    return cleanName(std::string(line.substr(10)));
                }
                if (!line.empty() && line[0] != '#') {
                    return "";   // the entries started: a title comes before them
                }
                pos = end + 1;
            }
            return "";
        }
    }

    std::string cleanName(const std::string &name) {
        std::string out;
        bool space = false;
        for (unsigned char c: name) {
            if (c <= 0x20 || c == 0x7F) {
                space = !out.empty();
                continue;
            }
            if (space) {
                out += ' ';
                space = false;
            }
            out += (char) c;
        }
        if (out.size() > MAX_NAME) {
            size_t cut = MAX_NAME;
            while (cut > 0 && ((unsigned char) out[cut] & 0xC0) == 0x80) {
                cut--;   // never inside a character
            }
            out.resize(cut);
            while (!out.empty() && out.back() == ' ') {
                out.pop_back();
            }
        }
        return out;
    }

    bool validUrl(const std::string &url) {
        if (url.empty() || url.size() > MAX_URL) {
            return false;
        }
        for (unsigned char c: url) {
            if (c <= 0x20 || c == 0x7F) {
                return false;
            }
        }
        std::string scheme = m3u::schemeOf(url);
        static const char *const ALLOWED[] = {"http", "https", "rtmp", "rtmps", "rtsp", "rtp", "udp", "mms"};
        if (std::find_if(std::begin(ALLOWED), std::end(ALLOWED), [&scheme](const char *s) { return scheme == s; })
            == std::end(ALLOWED)) {
            return false;
        }
        size_t host = scheme.size() + 3;
        return url.size() > host && url[host] != '/' && url[host] != '?' && url[host] != '#';
    }

    std::string slug(const std::string &title) {
        std::string out;
        bool dash = false;
        for (unsigned char c: title) {
            bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
            if (!keep) {
                dash = !out.empty();
                continue;
            }
            if (dash) {
                out += '-';
                dash = false;
            }
            out += (char) (c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            if (out.size() >= 40) {
                break;
            }
        }
        return out.empty() ? "playlist" : out;
    }

    bool fromM3u(std::string_view text, const std::string &fallbackTitle, Playlist &out, std::string &error) {
        m3u::Limits limits;
        limits.maxEntries = (int) MAX_ENTRIES + 1;
        m3u::Result r = m3u::parse(text, limits);
        if (r.error == m3u::Error::HlsMedia || r.error == m3u::Error::Utf16) {
            error = r.error == m3u::Error::HlsMedia ? "a single stream, not a playlist" : "UTF-16 text";
            return false;
        }
        if (r.entries.empty() && !r.stats.header) {
            error = "not an M3U playlist";
            return false;
        }
        if (r.entries.size() > MAX_ENTRIES || r.stats.overLimit > 0) {
            error = "more than " + std::to_string(MAX_ENTRIES) + " entries";
            return false;
        }
        Playlist p;
        p.path = out.path;
        p.title = titleOf(text);
        if (p.title.empty()) {
            p.title = cleanName(fallbackTitle);
        }
        p.entries.reserve(r.entries.size());
        for (auto &e: r.entries) {
            Entry x;
            x.name = cleanName(e.name);
            x.url = std::move(e.url);
            x.logo = std::move(e.logo);
            x.group = cleanName(e.group);
            x.tvgId = m3u::usableTvgId(e.tvgId) ? std::move(e.tvgId) : std::string();
            x.userAgent = std::move(e.userAgent);
            x.sourceId = std::move(e.sourceRef);
            x.channelId = std::move(e.channelRef);
            p.entries.push_back(std::move(x));
        }
        out = std::move(p);
        return true;
    }

    std::string toM3u(const Playlist &p) {
        std::string s = "#EXTM3U\n#PLAYLIST:" + cleanName(p.title) + "\n";
        for (const auto &e: p.entries) {
            s += "#EXTINF:-1";
            if (!e.tvgId.empty()) {
                s += " tvg-id=\"" + attr(e.tvgId) + "\"";
            }
            if (!e.logo.empty()) {
                s += " tvg-logo=\"" + attr(e.logo) + "\"";
            }
            if (!e.group.empty()) {
                s += " group-title=\"" + attr(e.group) + "\"";
            }
            if (!e.sourceId.empty() && !e.channelId.empty()) {
                s += " v7-source=\"" + attr(e.sourceId) + "\" v7-channel=\"" + attr(e.channelId) + "\"";
            }
            s += "," + cleanName(e.name) + "\n";
            if (!e.userAgent.empty()) {
                s += "#EXTVLCOPT:http-user-agent=" + attr(e.userAgent) + "\n";
            }
            s += e.url + "\n";
        }
        return s;
    }

    std::vector<Summary> list(const std::string &dir) {
        std::vector<Summary> out;
        for (const auto &f: fs::listDir(dir)) {
            if (f.dir || !playlistFile(f.name) || f.name[0] == '.' || out.size() >= MAX_PLAYLISTS) {
                continue;
            }
            Summary s;
            s.file = f.name;
            s.path = fs::join(dir, f.name);
            s.size = f.size;
            s.title = stem(f.name);
            std::string text;
            Playlist p;
            std::string err;
            if (f.size >= 0 && (size_t) f.size <= MAX_FILE_BYTES && fs::readFile(s.path, text, MAX_FILE_BYTES)
                && fromM3u(text, s.title, p, err)) {
                s.title = p.title;
                s.entries = (int) p.entries.size();
            } else {
                s.editable = false;
                s.entries = -1;
            }
            out.push_back(std::move(s));
        }
        std::sort(out.begin(), out.end(), [](const Summary &a, const Summary &b) {
            std::string la = lower(a.title);
            std::string lb = lower(b.title);
            return la != lb ? la < lb : a.file < b.file;
        });
        return out;
    }

    bool load(const std::string &path, Playlist &out, std::string &error) {
        std::string text;
        if (!fs::readFile(path, text, MAX_FILE_BYTES, &error)) {
            return false;
        }
        out.path = path;
        return fromM3u(text, stem(path), out, error);
    }

    bool save(const Playlist &p, std::string &error) {
        size_t slash = p.path.find_last_of('/');
        if (slash != std::string::npos && !fs::ensureDir(p.path.substr(0, slash + 1), &error)) {
            return false;
        }
        return fs::writeFileAtomic(p.path, toM3u(p), &error);
    }

    bool remove(const std::string &path) {
        bool ok = fs::removeFile(path);
        fs::removeFile(path + ".bak");
        return ok;
    }

    std::string newPath(const std::string &dir, const std::string &title) {
        std::string base = slug(title);
        std::string path = fs::join(dir, base + ".m3u");
        for (int n = 2; fs::exists(path) && n < 10000; n++) {
            path = fs::join(dir, base + "-" + std::to_string(n) + ".m3u");
        }
        return path;
    }

    bool inside(const std::string &dir, const std::string &path) {
        std::string d = dir;
        if (d.empty()) {
            return false;
        }
        if (d.back() != '/') {
            d += '/';
        }
        if (path.size() <= d.size() || path.compare(0, d.size(), d) != 0) {
            return false;
        }
        std::string name = path.substr(d.size());
        return name.find('/') == std::string::npos && name.find('\\') == std::string::npos && name[0] != '.'
               && playlistFile(name);
    }

    int move(Playlist &p, int index, int delta) {
        int n = (int) p.entries.size();
        if (index < 0 || index >= n) {
            return -1;
        }
        int target = std::min(std::max(index + delta, 0), n - 1);
        if (target > index) {
            std::rotate(p.entries.begin() + index, p.entries.begin() + index + 1, p.entries.begin() + target + 1);
        } else if (target < index) {
            std::rotate(p.entries.begin() + target, p.entries.begin() + index, p.entries.begin() + index + 1);
        }
        return target;
    }

    int find(const Playlist &p, const std::string &url) {
        for (size_t i = 0; i < p.entries.size(); i++) {
            if (p.entries[i].url == url) {
                return (int) i;
            }
        }
        return -1;
    }
}
