#include <cctype>
#include <chrono>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "catalog.h"
#include "m3u.h"
#include "../core/utf8.h"

namespace m3u {

    const char *const LOCAL_PLAYLIST_DIR = "/data/PS4IPTV/playlists/";

    namespace {

        bool isSpace(char c) {
            return c == ' ' || c == '\t' || c == '\v' || c == '\f';
        }

        std::string_view trim(std::string_view s) {
            size_t a = 0;
            size_t b = s.size();
            while (a < b && isSpace(s[a])) {
                a++;
            }
            while (b > a && isSpace(s[b - 1])) {
                b--;
            }
            return s.substr(a, b - a);
        }

        char lowerAscii(char c) {
            return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
        }

        std::string lower(std::string_view s) {
            std::string out(s);
            for (char &c: out) {
                c = lowerAscii(c);
            }
            return out;
        }

        // case-insensitive "starts with" for directive names
        bool startsWithNoCase(std::string_view s, std::string_view prefix) {
            if (s.size() < prefix.size()) {
                return false;
            }
            for (size_t i = 0; i < prefix.size(); i++) {
                if (lowerAscii(s[i]) != lowerAscii(prefix[i])) {
                    return false;
                }
            }
            return true;
        }

        bool endsWithNoCase(std::string_view s, std::string_view suffix) {
            return s.size() >= suffix.size() && startsWithNoCase(s.substr(s.size() - suffix.size()), suffix);
        }

        // strict UTF-8: no overlong forms, surrogates or code points above U+10FFFF
        bool validUtf8(std::string_view s) {
            size_t i = 0;
            while (i < s.size()) {
                auto c = (unsigned char) s[i];
                if (c < 0x80) {
                    i++;
                    continue;
                }
                size_t len;
                char32_t cp;
                if ((c & 0xE0) == 0xC0) {
                    len = 2;
                    cp = c & 0x1F;
                } else if ((c & 0xF0) == 0xE0) {
                    len = 3;
                    cp = c & 0x0F;
                } else if ((c & 0xF8) == 0xF0) {
                    len = 4;
                    cp = c & 0x07;
                } else {
                    return false;
                }
                if (i + len > s.size()) {
                    return false;
                }
                for (size_t k = 1; k < len; k++) {
                    auto cc = (unsigned char) s[i + k];
                    if ((cc & 0xC0) != 0x80) {
                        return false;
                    }
                    cp = (cp << 6) | (cc & 0x3F);
                }
                if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)
                    || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                    return false;
                }
                i += len;
            }
            return true;
        }

        // text shown in the UI / written to the history file: always valid UTF-8
        std::string cleanText(std::string_view s, int &repaired) {
            if (validUtf8(s)) {
                return std::string(s);
            }
            repaired++;
            std::string out;
            for (char32_t cp: utf8::decode(std::string(s))) {
                out += utf8::encode(cp);
            }
            return out;
        }

        // value without surrounding quotes (EXTVLCOPT values are sometimes quoted)
        std::string_view unquote(std::string_view v) {
            v = trim(v);
            if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) {
                return v.substr(1, v.size() - 2);
            }
            return v;
        }

        struct Authority {
            std::string scheme;
            std::string_view host;   // [user@]host[:port] with the user info removed
            std::string_view path;   // from the first '/' after the authority, up to '?' / '#'
        };

        bool splitUrl(std::string_view url, Authority &a) {
            a.scheme = schemeOf(url);
            if (a.scheme.empty()) {
                return false;
            }
            size_t start = url.find("://") + 3;
            size_t end = start;
            while (end < url.size() && url[end] != '/' && url[end] != '?' && url[end] != '#') {
                end++;
            }
            std::string_view authority = url.substr(start, end - start);
            size_t at = authority.rfind('@');
            if (at != std::string_view::npos) {
                authority = authority.substr(at + 1);
            }
            a.host = authority;
            size_t pathEnd = end;
            while (pathEnd < url.size() && url[pathEnd] != '?' && url[pathEnd] != '#') {
                pathEnd++;
            }
            a.path = url.substr(end, pathEnd - end);
            return !a.host.empty();
        }

        // last path segment without its extension ("TRT1" for ".../TRT1.m3u8")
        std::string nameFromUrl(std::string_view url) {
            Authority a;
            if (!splitUrl(url, a)) {
                return "";
            }
            std::string_view path = a.path;
            while (!path.empty() && path.back() == '/') {
                path.remove_suffix(1);
            }
            size_t slash = path.rfind('/');
            std::string_view last = slash == std::string_view::npos ? path : path.substr(slash + 1);
            size_t dot = last.rfind('.');
            if (dot != std::string_view::npos && dot > 0) {
                last = last.substr(0, dot);
            }
            return last.empty() ? std::string(a.host) : std::string(last);
        }

        uint64_t fnv64(std::string_view s) {
            uint64_t h = 1469598103934665603ull;
            for (unsigned char c: s) {
                h ^= c;
                h *= 1099511628211ull;
            }
            return h;
        }

        uint32_t fnv32(std::string_view s) {
            uint32_t h = 2166136261u;
            for (unsigned char c: s) {
                h ^= c;
                h *= 16777619u;
            }
            return h;
        }

        std::string hex(uint64_t v, int digits) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%0*llx", digits, (unsigned long long) v);
            return buf;
        }

        // tvg-id values that identify nothing
        bool usableTvgId(const std::string &id) {
            if (id.empty()) {
                return false;
            }
            std::string l = lower(id);
            return l != "none" && l != "null" && l != "n/a" && l != "-" && l != "0" && l != "(no tvg-id)";
        }

        // "  TRT   1 " -> "TRT 1" (white space changes do not change the identity)
        std::string collapse(const std::string &s) {
            std::string out;
            bool space = false;
            for (char c: s) {
                if (isSpace(c)) {
                    space = !out.empty();
                    continue;
                }
                if (space) {
                    out += ' ';
                    space = false;
                }
                out += c;
            }
            return out;
        }

        // #EXTINF:<duration> key="value" key2=value2,<title>
        void parseExtinf(std::string_view s, Entry &e, bool &malformed) {
            size_t i = 0;
            size_t n = s.size();
            while (i < n && isSpace(s[i])) {
                i++;
            }
            while (i < n && !isSpace(s[i]) && s[i] != ',') {
                i++;   // duration (-1 for live channels), unused
            }
            bool titled = false;
            while (i < n) {
                while (i < n && isSpace(s[i])) {
                    i++;
                }
                if (i >= n) {
                    break;
                }
                if (s[i] == ',') {
                    e.name = std::string(trim(s.substr(i + 1)));
                    titled = true;
                    break;
                }
                size_t keyStart = i;
                while (i < n && s[i] != '=' && !isSpace(s[i]) && s[i] != ',') {
                    i++;
                }
                std::string key = lower(s.substr(keyStart, i - keyStart));
                if (i >= n || s[i] != '=') {
                    continue;   // a bare word: ignored
                }
                i++;
                std::string_view value;
                if (i < n && (s[i] == '"' || s[i] == '\'')) {
                    char q = s[i++];
                    size_t close = s.find(q, i);
                    if (close == std::string_view::npos) {
                        // unterminated quote: the value runs to the last comma, the title follows it
                        malformed = true;
                        size_t comma = s.rfind(',');
                        if (comma != std::string_view::npos && comma >= i) {
                            value = s.substr(i, comma - i);
                            e.name = std::string(trim(s.substr(comma + 1)));
                            titled = true;
                        } else {
                            value = s.substr(i);
                        }
                        i = n;
                    } else {
                        value = s.substr(i, close - i);
                        i = close + 1;
                    }
                } else {
                    size_t start = i;
                    while (i < n && !isSpace(s[i]) && s[i] != ',') {
                        i++;
                    }
                    value = s.substr(start, i - start);
                }
                value = trim(value);
                if (key == "tvg-id") {
                    e.tvgId = std::string(value);
                } else if (key == "tvg-name") {
                    e.tvgName = std::string(value);
                } else if (key == "tvg-logo") {
                    e.logo = std::string(value);
                } else if (key == "group-title") {
                    e.group = std::string(value);
                } else if (key == "tvg-chno" || key == "channel-number" || key == "tvg-channel-number") {
                    int num = 0;
                    for (char c: value) {
                        if (c < '0' || c > '9' || num > 99999) {
                            num = 0;
                            break;
                        }
                        num = num * 10 + (c - '0');
                    }
                    e.number = num;
                } else if (key == "user-agent" || key == "http-user-agent") {
                    e.userAgent = std::string(value);
                }
            }
            if (!titled) {
                malformed = true;   // no title comma
            }
        }
    }

    std::string schemeOf(std::string_view url) {
        size_t sep = url.find("://");
        if (sep == std::string_view::npos || sep == 0 || sep > 16) {
            return "";
        }
        if (!std::isalpha((unsigned char) url[0])) {
            return "";
        }
        for (size_t i = 0; i < sep; i++) {
            char c = url[i];
            if (!std::isalnum((unsigned char) c) && c != '+' && c != '-' && c != '.') {
                return "";
            }
        }
        return lower(url.substr(0, sep));
    }

    bool isHttpUrl(const std::string &url) {
        Authority a;
        return splitUrl(url, a) && (a.scheme == "http" || a.scheme == "https");
    }

    std::string stableUrlKey(const std::string &url) {
        Authority a;
        if (!splitUrl(url, a)) {
            return url;
        }
        return a.scheme + "://" + lower(a.host) + std::string(a.path);
    }

    std::string localPlaylistPath(const std::string &input) {
        std::string_view s = trim(input);
        if (startsWithNoCase(s, "file://")) {
            s.remove_prefix(7);
        }
        std::string_view dir = LOCAL_PLAYLIST_DIR;
        if (s.size() <= dir.size() || s.compare(0, dir.size(), dir) != 0) {
            return "";
        }
        std::string_view name = s.substr(dir.size());
        if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos
            || name.find("..") != std::string_view::npos || name.front() == '.') {
            return "";
        }
        if (!endsWithNoCase(name, ".m3u") && !endsWithNoCase(name, ".m3u8")) {
            return "";
        }
        return std::string(s);
    }

    std::string displayUrl(const std::string &url) {
        std::string local = localPlaylistPath(url);
        if (!local.empty()) {
            return "playlists/" + local.substr(std::string(LOCAL_PLAYLIST_DIR).size());
        }
        Authority a;
        if (!splitUrl(url, a)) {
            return "";
        }
        std::string_view path = a.path;
        while (!path.empty() && path.back() == '/') {
            path.remove_suffix(1);
        }
        size_t slash = path.rfind('/');
        std::string_view last = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
        std::string out = lower(a.host);
        if (!last.empty()) {
            out += slash == 0 ? "/" : "/\xE2\x80\xA6/";   // host/…/name: inner segments may be credentials
            out += std::string(last);
        }
        return out;
    }

    Result parse(std::string_view text, const Limits &limits) {
        Result r;
        Stats &st = r.stats;
        st.bytes = text.size();
        if (text.size() >= 2 && (((unsigned char) text[0] == 0xFF && (unsigned char) text[1] == 0xFE)
                                 || ((unsigned char) text[0] == 0xFE && (unsigned char) text[1] == 0xFF))) {
            r.error = Error::Utf16;
            return r;
        }
        if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            text.remove_prefix(3);
        }

        Entry pending;
        bool hasPending = false;
        bool pendingMalformed = false;
        std::string pendingGroup;    // #EXTGRP
        std::string pendingAgent;    // #EXTVLCOPT:http-user-agent
        bool hlsMarker = false;
        bool anyContent = false;
        std::unordered_set<std::string> groups;

        auto resetPending = [&]() {
            pending = Entry();
            hasPending = false;
            pendingMalformed = false;
            pendingGroup.clear();
            pendingAgent.clear();
        };

        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = pos;
            while (end < text.size() && text[end] != '\n' && text[end] != '\r') {
                end++;
            }
            std::string_view line = trim(text.substr(pos, end - pos));
            pos = end;
            if (pos < text.size() && text[pos] == '\r') {
                pos++;
            }
            if (pos < text.size() && text[pos] == '\n') {
                pos++;
            }
            st.lines++;
            if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
                line = trim(line.substr(3));   // a BOM in the middle (concatenated playlists)
            }
            if (line.empty()) {
                continue;
            }
            anyContent = true;
            if (line.size() > limits.maxLineBytes) {
                if (line[0] != '#') {
                    st.invalidUrl++;
                    resetPending();
                }
                continue;
            }
            if (line[0] == '#') {
                if (startsWithNoCase(line, "#EXTM3U")) {
                    st.header = true;
                } else if (startsWithNoCase(line, "#EXTINF:")) {
                    if (hasPending) {
                        st.missingUrl++;   // the previous entry never got its URL
                        // #EXTGRP / #EXTVLCOPT written before this #EXTINF belong to the new entry
                        pending = Entry();
                        pendingMalformed = false;
                    }
                    st.extinf++;
                    parseExtinf(line.substr(8), pending, pendingMalformed);
                    hasPending = true;
                } else if (startsWithNoCase(line, "#EXTGRP:")) {
                    pendingGroup = std::string(trim(line.substr(8)));
                } else if (startsWithNoCase(line, "#EXTVLCOPT:")) {
                    std::string_view opt = trim(line.substr(11));
                    if (startsWithNoCase(opt, "http-user-agent=")) {
                        pendingAgent = std::string(unquote(opt.substr(16)));
                    }
                    // other VLC options (http-referrer, network-caching...) have no use here
                } else if (startsWithNoCase(line, "#EXT-X-")) {
                    if (startsWithNoCase(line, "#EXT-X-TARGETDURATION") || startsWithNoCase(line, "#EXT-X-STREAM-INF")
                        || startsWithNoCase(line, "#EXT-X-MEDIA-SEQUENCE") || startsWithNoCase(line, "#EXT-X-ENDLIST")
                        || startsWithNoCase(line, "#EXT-X-PLAYLIST-TYPE")) {
                        hlsMarker = true;
                    }
                } else if (startsWithNoCase(line, "#KODIPROP:")) {
                    // Kodi inputstream properties (DRM, manifest type): no use here
                } else if (startsWithNoCase(line, "#EXT")) {
                    st.unknownDirectives++;
                }
                continue;   // other '#' lines are comments
            }

            // a media line
            Authority a;
            if (!splitUrl(line, a)) {
                st.invalidUrl++;
                resetPending();
                continue;
            }
            if ((int) r.entries.size() >= limits.maxEntries) {
                st.overLimit++;
                resetPending();
                continue;
            }
            Entry e = hasPending ? std::move(pending) : Entry();
            if (!hasPending) {
                st.plainUrls++;
            }
            if (pendingMalformed) {
                st.malformedExtinf++;
            }
            e.url = std::string(line);
            e.group = std::string(trim(e.group));
            if (e.group.empty()) {
                e.group = pendingGroup;   // #EXTGRP only when group-title is absent
            }
            if (e.userAgent.empty()) {
                e.userAgent = pendingAgent;
            }
            if (e.name.empty()) {
                e.name = e.tvgName;
            }
            if (e.name.empty()) {
                st.unnamed++;
                e.name = nameFromUrl(line);
            }
            int repaired = 0;
            e.name = cleanText(e.name, repaired);
            e.group = cleanText(e.group, repaired);
            e.tvgName = cleanText(e.tvgName, repaired);
            st.invalidUtf8 += repaired;
            e.tvgId = std::string(trim(e.tvgId));

            st.channels++;
            if (e.logo.empty()) {
                st.withoutLogo++;
            } else {
                st.withLogo++;
            }
            if (e.group.empty()) {
                st.uncategorized++;
            } else {
                groups.insert(e.group);
            }
            st.withTvgId += usableTvgId(e.tvgId);
            st.userAgents += !e.userAgent.empty();
            if (a.scheme == "https") {
                st.https++;
            } else if (a.scheme != "http") {
                st.otherProtocols++;
            }
            st.hls += endsWithNoCase(a.path, ".m3u8");
            r.entries.push_back(std::move(e));
            resetPending();
        }
        if (hasPending) {
            st.missingUrl++;
        }
        st.groups = (int) groups.size();

        if (hlsMarker) {
            r.error = Error::HlsMedia;
            r.entries.clear();
            st.channels = 0;
        } else if (!anyContent) {
            r.error = Error::Empty;
        } else if (r.entries.empty()) {
            r.error = Error::NotPlaylist;
        }
        return r;
    }

    std::string channelId(const std::string &sourceId, const Entry &e) {
        std::string base = sourceId;
        base += '\x1f';
        if (usableTvgId(e.tvgId)) {
            base += e.tvgId;
        }
        base += '\x1f';
        base += e.group;
        base += '\x1f';
        base += collapse(e.name);
        return "m" + hex(fnv64(base), 16);
    }

    void toChannels(const std::string &sourceId, const std::vector<Entry> &entries,
                    std::vector<iptv::Category> &categories, std::vector<iptv::LiveChannel> &channels,
                    Stats *stats) {
        categories.clear();
        channels.clear();
        channels.reserve(entries.size());
        std::unordered_set<std::string> used;
        used.reserve(entries.size() * 2);
        std::unordered_map<std::string, int> groupIndex;
        bool uncategorized = false;
        int duplicates = 0;
        for (const Entry &e: entries) {
            iptv::LiveChannel c;
            std::string id = channelId(sourceId, e);
            if (!used.insert(id).second) {
                duplicates++;
                std::string withUrl = id + "-" + hex(fnv32(stableUrlKey(e.url)), 8);
                id = withUrl;
                for (int n = 2; !used.insert(id).second; n++) {
                    id = withUrl + "-" + std::to_string(n);
                }
            }
            c.id = std::move(id);
            c.name = e.name;
            c.icon = e.logo;
            c.epgId = e.tvgId;
            c.url = e.url;
            c.userAgent = e.userAgent;
            c.num = e.number;
            if (e.group.empty()) {
                c.categoryId = iptv::UNCATEGORIZED_ID;
                uncategorized = true;
            } else {
                c.categoryId = e.group;
                if (groupIndex.emplace(e.group, (int) categories.size()).second) {
                    categories.push_back({e.group, e.group, ""});
                }
            }
            channels.push_back(std::move(c));
        }
        if (uncategorized) {
            categories.push_back({iptv::UNCATEGORIZED_ID, iptv::uncategorizedName(), ""});
        }
        if (stats) {
            stats->duplicateIds = duplicates;
        }
    }

    Error build(const std::string &sourceId, std::string_view body, iptv::LiveCatalog &out, Info &info,
                const Limits &limits) {
        using clock = std::chrono::steady_clock;
        auto t0 = clock::now();
        Result r = parse(body, limits);
        info.stats = r.stats;
        if (!r.ok()) {
            return r.error == Error::None ? Error::NotPlaylist : r.error;
        }
        std::vector<iptv::Category> categories;
        std::vector<iptv::LiveChannel> channels;
        toChannels(sourceId, r.entries, categories, channels, &info.stats);
        r.entries.clear();
        r.entries.shrink_to_fit();
        auto t1 = clock::now();
        info.categories = (int) categories.size();
        iptv::LiveCatalog catalog;
        catalog.assign(std::move(categories), std::move(channels));
        auto t2 = clock::now();
        info.parseMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        info.indexMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
        info.indexBytes = catalog.searchIndex().memoryBytes();
        out = std::move(catalog);
        return Error::None;
    }
}
