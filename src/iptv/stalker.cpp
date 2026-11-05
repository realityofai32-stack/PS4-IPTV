#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "stalker.h"
#include "xtream.h"
#include "../core/hash.h"
#include "../core/url.h"

namespace stalker {

    const char *const USER_AGENT = "Mozilla/5.0 (QtEmbedded; U; Linux; C) AppleWebKit/533.3 (KHTML, like Gecko) "
                                   "MAG200 stbapp ver: 2 rev: 250 Safari/533.3";
    const char *const X_USER_AGENT = "Model: MAG250; Link: WiFi";

    namespace {

        // portal numbers: digits only, clamped (never undefined for out-of-range text, unlike atoi / atof)
        int toInt(const std::string &text) {
            const char *b = text.c_str();
            char *e = nullptr;
            long v = std::strtol(b, &e, 10);
            if (e == b) {
                return 0;
            }
            return v > 1000000000L ? 1000000000 : v < -1000000000L ? -1000000000 : (int) v;
        }

        double toDouble(const std::string &text) {
            const char *b = text.c_str();
            char *e = nullptr;
            double v = std::strtod(b, &e);
            return e == b || !(v == v) ? 0.0 : v;   // no number, or NaN
        }

        bool startsWith(const std::string &s, const char *p) {
            return s.compare(0, std::char_traits<char>::length(p), p) == 0;
        }

        bool endsWith(const std::string &s, const std::string &suffix) {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::string trimmed(const std::string &s) {
            return url::trim(s);
        }

        // id fields come as numbers or strings
        std::string idOf(const json::Value &v) {
            return trimmed(v.asString());
        }

        float ratingOf(const json::Value &o) {
            for (const char *k: {"rating_imdb", "rating_kinopoisk", "rating"}) {
                double r = toDouble(o[k].asString());
                if (r > 0 && r <= 10) {
                    return (float) r;
                }
            }
            return 0;
        }

        std::vector<int> intList(const json::Value &v) {
            std::vector<int> out;
            if (v.isArray()) {
                for (const auto &x: v.items()) {
                    int n = (int) x.asInt(0);
                    if (n > 0) {
                        out.push_back(n);
                    }
                }
            }
            return out;
        }

        // the list of a page: {"data": [...]} (paged) or the array itself
        const json::Value *listOf(const json::Value &js, Page *page) {
            if (js.isArray()) {
                if (page) {
                    page->count = (int) js.size();
                }
                return &js;
            }
            if (!js.isObject() || !js["data"].isArray()) {
                return nullptr;
            }
            if (page) {
                page->totalItems = js.has("total_items") ? (int) js["total_items"].asInt(-1) : -1;
                page->perPage = (int) js["max_page_items"].asInt(0);
                page->count = (int) js["data"].size();
            }
            return &js["data"];
        }

        std::string firstOf(const json::Value &o, std::initializer_list<const char *> keys) {
            for (const char *k: keys) {
                std::string v = trimmed(o[k].asString());
                if (!v.empty()) {
                    return v;
                }
            }
            return "";
        }

        int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
            y -= m <= 2;
            const int64_t era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = (unsigned) (y - era * 400);
            const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + (int64_t) doe - 719468;
        }

        std::string extensionOf(const std::string &u) {
            std::string path = u.substr(0, u.find_first_of("?#"));
            size_t slash = path.rfind('/');
            size_t dot = path.rfind('.');
            if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
                return "";
            }
            std::string ext = url::lower(path.substr(dot + 1));
            bool alnum = !ext.empty() && ext.size() <= 5
                         && std::all_of(ext.begin(), ext.end(), [](char c) { return std::isalnum((unsigned char) c); });
            return alnum && ext != "php" ? ext : "";
        }
    }

    bool normalizeMac(const std::string &input, std::string &out) {
        std::string hex;
        for (char c: input) {
            if (std::isxdigit((unsigned char) c)) {
                hex += (char) std::toupper((unsigned char) c);
            } else if (c != ':' && c != '-' && c != ' ' && c != '.') {
                return false;
            }
        }
        if (hex.size() != 12) {
            return false;
        }
        out.clear();
        for (size_t i = 0; i < 12; i += 2) {
            out += (i ? ":" : "") + hex.substr(i, 2);
        }
        return true;
    }

    std::string maskedMac(const std::string &mac) {
        std::string n;
        if (!normalizeMac(mac, n)) {
            return "\xE2\x80\xA2\xE2\x80\xA2:\xE2\x80\xA2\xE2\x80\xA2";
        }
        // the vendor prefix (e.g. 00:1A:79 for MAG boxes) and the last byte: enough to tell two boxes apart
        return n.substr(0, 8) + ":\xE2\x80\xA2\xE2\x80\xA2:\xE2\x80\xA2\xE2\x80\xA2:" + n.substr(15, 2);
    }

    Identity identity(const std::string &mac) {
        Identity id;
        id.mac = mac;
        id.serial = hash::md5Hex(mac).substr(0, 13);
        id.deviceId = hash::sha256Hex(id.serial);
        id.deviceId2 = hash::sha256Hex(mac);
        id.signature = hash::sha256Hex(id.serial + mac);
        return id;
    }

    Portal portalFor(const std::string &input) {
        Portal p;
        std::string s = trimmed(input);
        if (s.empty()) {
            p.error = "stalker.error.portal";
            return p;
        }
        std::string lowered = url::lower(s);
        if (lowered.find("://") == std::string::npos) {
            s = "http://" + s;
        } else if (!startsWith(lowered, "http://") && !startsWith(lowered, "https://")) {
            p.error = "stalker.error.portal_scheme";
            return p;
        }
        size_t hostStart = s.find("://") + 3;
        size_t pathStart = s.find_first_of("/?#", hostStart);
        std::string authority = s.substr(hostStart, pathStart == std::string::npos ? std::string::npos
                                                                                  : pathStart - hostStart);
        if (authority.empty() || authority.find('@') != std::string::npos || authority.find(' ') != std::string::npos) {
            p.error = "stalker.error.portal";
            return p;
        }
        std::string scheme = url::lower(s.substr(0, hostStart - 3));
        p.origin = scheme + "://" + authority;
        std::string path = pathStart == std::string::npos ? "" : s.substr(pathStart);
        path = path.substr(0, path.find_first_of("?#"));
        if (endsWith(url::lower(path), ".php")) {
            // a full API address was pasted: use it as it is
            p.endpoints.push_back(p.origin + path);
            size_t slash = path.rfind('/');
            std::string dir = path.substr(0, slash);
            if (endsWith(dir, "/server")) {
                dir = dir.substr(0, dir.size() - 7);
            }
            p.referer = p.origin + dir + "/c/";
            p.ok = true;
            return p;
        }
        while (!path.empty() && path.back() == '/') {
            path.pop_back();
        }
        if (endsWith(path, "/c")) {
            path = path.substr(0, path.size() - 2);
        }
        std::vector<std::string> list;
        auto addUnique = [&list](const std::string &u) {
            if (std::find(list.begin(), list.end(), u) == list.end()) {
                list.push_back(u);
            }
        };
        if (path.find("stalker_portal") != std::string::npos) {
            addUnique(p.origin + path + "/server/load.php");
        }
        if (!path.empty()) {
            addUnique(p.origin + path + "/portal.php");
        }
        addUnique(p.origin + "/portal.php");
        addUnique(p.origin + "/stalker_portal/server/load.php");
        if (!path.empty()) {
            addUnique(p.origin + path + "/server/load.php");
        }
        p.endpoints = list;
        p.referer = p.origin + path + "/c/";
        p.ok = true;
        return p;
    }

    std::string apiUrl(const std::string &endpoint, const std::string &type, const std::string &action,
                       const std::string &params) {
        return endpoint + "?type=" + url::encode(type) + "&action=" + url::encode(action) + params
               + "&JsHttpRequest=1-xml";
    }

    std::vector<std::string> headers(const Identity &id, const std::string &referer, const std::string &token) {
        std::vector<std::string> h = {
                std::string("X-User-Agent: ") + X_USER_AGENT,
                "Referer: " + referer,
                "Cookie: mac=" + url::encode(id.mac) + "; stb_lang=en; timezone=UTC",
                "Accept: */*"};
        if (!token.empty()) {
            h.push_back("Authorization: Bearer " + token);
        }
        return h;
    }

    std::string profileParams(const Identity &id, int64_t timestamp) {
        const std::string ver = "ImageDescription: 0.2.18-r23-250; ImageDate: Fri Jan 15 15:20:44 EET 2016; "
                                "PORTAL version: 5.6.1; API Version: JS API version: 343; STB API version: 146; "
                                "Player Engine version: 0x58c";
        return "&hd=1&ver=" + url::encode(ver) + "&num_banks=2&sn=" + id.serial
               + "&stb_type=MAG250&client_type=STB&image_version=218&video_out=hdmi&device_id=" + id.deviceId
               + "&device_id2=" + id.deviceId2 + "&signature=" + id.signature
               + "&auth_second_step=1&hw_version=1.7-BD-00&not_valid_token=0&timestamp=" + std::to_string(timestamp)
               + "&api_signature=262&prehash=";
    }

    Reply envelope(const std::string &body, json::Value &js) {
        std::string head = url::lower(trimmed(body.substr(0, 200)));
        if (head.find("authorization failed") != std::string::npos || head.find("unauthorized") == 0) {
            return Reply::AuthFailed;
        }
        json::Value root;
        if (!json::parse(body, root) || !root.isObject()) {
            return Reply::NotJson;
        }
        if (!root.has("js")) {
            return Reply::NoJs;
        }
        js = root["js"];
        return Reply::Ok;
    }

    bool parseToken(const std::string &body, std::string &token, std::string &error) {
        json::Value js;
        Reply r = envelope(body, js);
        if (r != Reply::Ok) {
            error = r == Reply::NotJson ? "not a portal response" : r == Reply::AuthFailed ? "handshake refused"
                                                                                            : "no js in the handshake";
            return false;
        }
        token = trimmed(js["token"].asString());
        if (token.empty()) {
            error = "handshake without a token";
            return false;
        }
        return true;
    }

    ProfileReply parseProfile(const std::string &body) {
        ProfileReply p;
        json::Value js;
        if (envelope(body, js) != Reply::Ok || !js.isObject()) {
            p.message = "";
            return p;
        }
        p.message = firstOf(js, {"block_msg", "msg"});
        // Ministra: status 0 = allowed, 2 = login and password needed; 1 (or a block message) = refused
        int64_t status = js["status"].asInt(0);
        bool blockedFlag = js["blocked"].asInt(0) == 1;
        std::string id = idOf(js["id"]);
        if (status == 2) {
            p.needsLogin = true;
            return p;
        }
        if (status != 0 || blockedFlag || ((id.empty() || id == "0") && !p.message.empty())) {
            p.blocked = true;
            return p;
        }
        p.ok = true;
        return p;
    }

    void parseAccountInfo(const std::string &body, iptv::AccountInfo &account) {
        json::Value js;
        if (envelope(body, js) != Reply::Ok || !js.isObject()) {
            return;
        }
        std::string end = firstOf(js, {"end_date", "expire_billing_date", "tariff_expired_date", "phone"});
        int64_t t = parseDateTime(end);
        if (t > 0) {
            account.expiresAt = t;
        }
        account.status = "Active";
    }

    bool parseCategories(const std::string &body, std::vector<iptv::Category> &out, std::string &error) {
        json::Value js;
        Reply r = envelope(body, js);
        if (r != Reply::Ok) {
            error = r == Reply::AuthFailed ? "authorization failed" : "not a category list";
            return false;
        }
        const json::Value *list = listOf(js, nullptr);
        if (list == nullptr) {
            error = "categories: unexpected shape";
            return false;
        }
        out.clear();
        std::set<std::string> seen;
        for (const auto &o: list->items()) {
            std::string id = idOf(o["id"]);
            if (id.empty() || id == "*" || !seen.insert(id).second) {
                continue;
            }
            iptv::Category c;
            c.id = id;
            c.name = xtream::cleanText(firstOf(o, {"title", "name", "alias"}));
            if (c.name.empty()) {
                c.name = id;
            }
            out.push_back(c);
        }
        return true;
    }

    bool parseChannels(const std::string &body, const std::string &origin, std::vector<iptv::LiveChannel> &out,
                       Page *page, std::string &error) {
        json::Value js;
        Reply r = envelope(body, js);
        if (r != Reply::Ok) {
            error = r == Reply::AuthFailed ? "authorization failed" : "not a channel list";
            return false;
        }
        const json::Value *list = listOf(js, page);
        if (list == nullptr) {
            error = "channels: unexpected shape";
            return false;
        }
        std::set<std::string> seen;
        for (const auto &c: out) {
            seen.insert(c.id);
        }
        for (const auto &o: list->items()) {
            std::string id = idOf(o["id"]);
            std::string cmd = trimmed(o["cmd"].asString());
            if (id.empty() || cmd.empty() || !seen.insert(id).second) {
                continue;
            }
            iptv::LiveChannel ch;
            ch.id = id;
            ch.name = xtream::cleanText(o["name"].asString());
            if (ch.name.empty()) {
                ch.name = id;
            }
            ch.categoryId = idOf(o["tv_genre_id"]);
            ch.icon = resolveImage(origin, o["logo"].asString());
            ch.epgId = trimmed(o["xmltv_id"].asString());
            ch.num = (int) o["number"].asInt(0);
            ch.archive = o["archive"].asInt(0) == 1 || o["tv_archive"].asInt(0) == 1;
            ch.cmd = cmd;
            out.push_back(ch);
        }
        return true;
    }

    bool parseVodPage(const std::string &body, const std::string &origin, std::vector<VodEntry> &out, Page &page,
                      std::string &error) {
        json::Value js;
        Reply r = envelope(body, js);
        if (r != Reply::Ok) {
            error = r == Reply::AuthFailed ? "authorization failed" : "not a list";
            return false;
        }
        const json::Value *list = listOf(js, &page);
        if (list == nullptr) {
            error = "list: unexpected shape";
            return false;
        }
        for (const auto &o: list->items()) {
            VodEntry e;
            e.id = idOf(o["id"]);
            if (e.id.empty() || !o.isObject()) {
                continue;
            }
            e.name = xtream::cleanText(firstOf(o, {"name", "o_name"}));
            e.categoryId = idOf(o["category_id"]);
            e.poster = resolveImage(origin, firstOf(o, {"screenshot_uri", "cover_big", "cover", "logo"}));
            e.cmd = trimmed(o["cmd"].asString());
            e.plot = xtream::cleanText(o["description"].asString());
            e.director = xtream::cleanText(o["director"].asString());
            e.cast = xtream::cleanText(o["actors"].asString());
            e.genre = xtream::cleanText(firstOf(o, {"genres_str", "genre"}));
            e.year = trimmed(o["year"].asString());
            e.rating = ratingOf(o);
            e.minutes = (int) o["time"].asInt(0);
            e.added = parseDateTime(o["added"].asString());
            e.episodes = intList(o["series"]);
            e.isSeries = o["is_series"].asInt(0) == 1 || !e.episodes.empty();
            out.push_back(std::move(e));
        }
        return true;
    }

    iptv::Movie toMovie(const VodEntry &e) {
        iptv::Movie m;
        m.streamId = e.id;
        m.name = e.name.empty() ? e.id : e.name;
        int fromName = 0;
        xtream::splitTitleYear(m.name, m.title, fromName);
        m.year = toInt(e.year.substr(0, 4));
        if (m.year < 1900 || m.year > 2100) {
            m.year = fromName;
        }
        m.categoryId = e.categoryId;
        m.icon = e.poster;
        m.extension = extensionOf(urlOfCmd(e.cmd));
        m.rating = e.rating;
        m.added = e.added;
        return m;
    }

    iptv::Series toSeries(const VodEntry &e, const std::string &seriesId) {
        iptv::Series s;
        s.seriesId = seriesId;
        s.name = e.name.empty() ? e.id : e.name;
        int fromName = 0;
        xtream::splitTitleYear(s.name, s.title, fromName);
        s.year = toInt(e.year.substr(0, 4));
        if (s.year < 1900 || s.year > 2100) {
            s.year = fromName;
        }
        s.categoryId = e.categoryId;
        s.cover = e.poster;
        s.plot = e.plot;
        s.cast = e.cast;
        s.director = e.director;
        s.genre = e.genre;
        s.releaseDate = e.year;
        s.rating = e.rating;
        s.runtimeMinutes = e.minutes;
        s.lastModified = e.added;
        return s;
    }

    iptv::MovieInfo toMovieInfo(const VodEntry &e) {
        iptv::MovieInfo i;
        i.streamId = e.id;
        i.plot = e.plot;
        i.genre = e.genre;
        i.director = e.director;
        i.cast = e.cast;
        i.releaseDate = e.year;
        i.coverBig = e.poster;
        i.rating = e.rating;
        i.durationSeconds = e.minutes > 0 ? e.minutes * 60 : 0;
        return i;
    }

    iptv::SeriesInfo seriesFromEntry(const VodEntry &e, const std::string &seriesId) {
        iptv::SeriesInfo info;
        info.seriesId = seriesId;
        info.series = toSeries(e, seriesId);
        iptv::Season season;
        season.number = 1;
        season.cover = e.poster;
        std::vector<int> numbers = e.episodes;
        std::sort(numbers.begin(), numbers.end());
        numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
        for (int n: numbers) {
            iptv::Episode ep;
            ep.id = seriesId + ":" + std::to_string(n);
            ep.season = 1;
            ep.number = n;
            ep.cmd = e.cmd;
            ep.linkSeries = n;
            ep.image = e.poster;
            ep.extension = extensionOf(urlOfCmd(e.cmd));
            season.episodes.push_back(ep);
        }
        if (!season.episodes.empty()) {
            info.seasons.push_back(season);
        }
        return info;
    }

    bool parseSeasons(const std::string &body, std::vector<SeasonEntry> &out, Page &page, std::string &error) {
        json::Value js;
        Reply r = envelope(body, js);
        if (r != Reply::Ok) {
            error = r == Reply::AuthFailed ? "authorization failed" : "not a season list";
            return false;
        }
        const json::Value *list = listOf(js, &page);
        if (list == nullptr) {
            error = "seasons: unexpected shape";
            return false;
        }
        for (const auto &o: list->items()) {
            SeasonEntry s;
            s.id = idOf(o["id"]);
            if (s.id.empty()) {
                continue;
            }
            s.name = xtream::cleanText(firstOf(o, {"name", "season_name"}));
            s.cmd = trimmed(o["cmd"].asString());
            s.episodes = intList(o["series"]);
            // "season_number", else "<series>:<n>" ids, else the digits of the name ("Season 3")
            int n = (int) o["season_number"].asInt(0);
            if (n <= 0) {
                size_t colon = s.id.rfind(':');
                if (colon != std::string::npos) {
                    n = toInt(s.id.substr(colon + 1));
                }
            }
            if (n <= 0) {
                for (size_t i = 0; i < s.name.size(); i++) {
                    if (std::isdigit((unsigned char) s.name[i])) {
                        n = toInt(s.name.substr(i));
                        break;
                    }
                }
            }
            s.number = n > 0 ? n : (int) out.size() + 1;
            out.push_back(s);
        }
        return true;
    }

    bool parseEpisodes(const std::string &body, const std::string &origin, int season, std::vector<iptv::Episode> &out,
                       Page &page, std::string &error) {
        json::Value js;
        Reply r = envelope(body, js);
        if (r != Reply::Ok) {
            error = r == Reply::AuthFailed ? "authorization failed" : "not an episode list";
            return false;
        }
        const json::Value *list = listOf(js, &page);
        if (list == nullptr) {
            error = "episodes: unexpected shape";
            return false;
        }
        for (const auto &o: list->items()) {
            iptv::Episode ep;
            ep.id = idOf(o["id"]);
            std::string cmd = trimmed(o["cmd"].asString());
            if (ep.id.empty() || cmd.empty()) {
                continue;
            }
            ep.season = season;
            ep.number = (int) o["series_number"].asInt(0);
            if (ep.number <= 0) {
                ep.number = (int) o["episode_number"].asInt((int64_t) out.size() + 1);
            }
            ep.title = xtream::cleanText(o["name"].asString());
            ep.plot = xtream::cleanText(o["description"].asString());
            ep.image = resolveImage(origin, firstOf(o, {"screenshot_uri", "cover"}));
            ep.durationSeconds = (int) o["time"].asInt(0) * 60;
            ep.cmd = cmd;
            ep.linkSeries = ep.number;
            ep.extension = extensionOf(urlOfCmd(cmd));
            out.push_back(ep);
        }
        return true;
    }

    bool parseLink(const std::string &body, std::string &out, std::string &error) {
        json::Value js;
        Reply r = envelope(body, js);
        if (r != Reply::Ok) {
            error = r == Reply::AuthFailed ? "authorization failed" : "not a link response";
            return false;
        }
        std::string portalError = trimmed(js["error"].asString());
        out = urlOfCmd(js.isObject() ? js["cmd"].asString() : js.asString());
        if (!portalError.empty() && portalError != "0") {
            error = portalError;
            return false;
        }
        if (out.empty()) {
            error = "no stream address in the link";
            return false;
        }
        return true;
    }

    std::string urlOfCmd(const std::string &cmd) {
        std::string s = trimmed(cmd);
        // "ffmpeg http://..", "ffrt3 http://..", "auto http://..": the address is the token with a scheme
        size_t start = 0;
        while (start < s.size()) {
            size_t end = s.find(' ', start);
            std::string tok = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
            std::string l = url::lower(tok);
            if (startsWith(l, "http://") || startsWith(l, "https://") || startsWith(l, "rtmp://")
                || startsWith(l, "udp://") || startsWith(l, "rtp://")) {
                return tok;
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return "";
    }

    bool cmdHasUrl(const std::string &cmd) {
        std::string u = urlOfCmd(cmd);
        std::string l = url::lower(u);
        return !u.empty() && l.find("://localhost") == std::string::npos && l.find("://127.0.0.1") == std::string::npos;
    }

    std::string resolveImage(const std::string &origin, const std::string &path) {
        std::string p = trimmed(path);
        std::string l = url::lower(p);
        if (startsWith(l, "http://") || startsWith(l, "https://")) {
            return p;
        }
        if (!p.empty() && p[0] == '/' && (p.size() < 2 || p[1] != '/') && !origin.empty()) {
            return origin + p;
        }
        return "";
    }

    int64_t parseDateTime(const std::string &text) {
        std::string t = trimmed(text);
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
        if (t.size() < 10 || std::sscanf(t.c_str(), "%4d-%2d-%2d", &y, &mo, &d) != 3) {
            return 0;
        }
        if (t.size() >= 19) {
            std::sscanf(t.c_str() + 11, "%2d:%2d:%2d", &h, &mi, &s);
        }
        if (y < 1970 || y > 2200 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59
            || s < 0 || s > 60) {
            return 0;
        }
        return daysFromCivil(y, (unsigned) mo, (unsigned) d) * 86400 + h * 3600 + mi * 60 + s;
    }

    std::string linkErrorKey(const std::string &portalError) {
        std::string e = url::lower(portalError);
        if (e.find("limit") != std::string::npos) {
            return "stalker.link.limit";
        }
        if (e.find("nothing_to_play") != std::string::npos || e.find("not_found") != std::string::npos) {
            return "stalker.link.nothing";
        }
        return "stalker.link.failed";
    }

    // ------------------------------------------------------------------ saved copies
    namespace {
        json::Value str(const std::string &v) {
            return json::Value::makeString(v);
        }

        std::string joinInts(const std::vector<int> &v) {
            std::string out;
            for (int n: v) {
                out += (out.empty() ? "" : ",") + std::to_string(n);
            }
            return out;
        }

        std::vector<int> splitInts(const std::string &s) {
            std::vector<int> out;
            size_t start = 0;
            while (start < s.size()) {
                size_t comma = s.find(',', start);
                int n = toInt(s.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
                if (n > 0) {
                    out.push_back(n);
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            return out;
        }
    }

    std::string encodeCategories(const std::vector<iptv::Category> &categories) {
        json::Value list = json::Value::makeArray();
        for (const auto &c: categories) {
            json::Value o = json::Value::makeObject();
            o.set("id", str(c.id));
            o.set("name", str(c.name));
            list.push(std::move(o));
        }
        return json::write(list, false);
    }

    bool decodeCategories(const std::string &text, std::vector<iptv::Category> &out, std::string &error) {
        out.clear();
        return json::forEachObject(text, [&out](const json::FlatObject &o) {
            if (!o.get("id").empty()) {
                out.push_back({o.get("id"), o.get("name"), ""});
            }
            return true;
        }, &error);
    }

    std::string encodeChannels(const std::vector<iptv::LiveChannel> &channels) {
        json::Value list = json::Value::makeArray();
        for (const auto &c: channels) {
            json::Value o = json::Value::makeObject();
            o.set("id", str(c.id));
            o.set("name", str(c.name));
            o.set("cat", str(c.categoryId));
            o.set("icon", str(c.icon));
            o.set("epg", str(c.epgId));
            o.set("num", json::Value::makeInt(c.num));
            o.set("archive", json::Value::makeBool(c.archive));
            o.set("cmd", str(c.cmd));
            list.push(std::move(o));
        }
        return json::write(list, false);
    }

    bool decodeChannels(const std::string &text, std::vector<iptv::LiveChannel> &out, std::string &error) {
        out.clear();
        return json::forEachObject(text, [&out](const json::FlatObject &o) {
            iptv::LiveChannel c;
            c.id = o.get("id");
            c.cmd = o.get("cmd");
            if (c.id.empty() || c.cmd.empty()) {
                return true;
            }
            c.name = o.get("name");
            c.categoryId = o.get("cat");
            c.icon = o.get("icon");
            c.epgId = o.get("epg");
            c.num = (int) o.getInt("num");
            c.archive = o.getBool("archive");
            out.push_back(std::move(c));
            return true;
        }, &error);
    }

    std::string encodeEntries(const std::vector<VodEntry> &entries) {
        json::Value list = json::Value::makeArray();
        for (const auto &e: entries) {
            json::Value o = json::Value::makeObject();
            o.set("id", str(e.id));
            o.set("name", str(e.name));
            o.set("cat", str(e.categoryId));
            o.set("poster", str(e.poster));
            o.set("cmd", str(e.cmd));
            o.set("plot", str(e.plot));
            o.set("director", str(e.director));
            o.set("cast", str(e.cast));
            o.set("genre", str(e.genre));
            o.set("year", str(e.year));
            o.set("rating", json::Value::makeNumber(e.rating));
            o.set("minutes", json::Value::makeInt(e.minutes));
            o.set("added", json::Value::makeInt(e.added));
            o.set("series", json::Value::makeBool(e.isSeries));
            o.set("episodes", str(joinInts(e.episodes)));
            list.push(std::move(o));
        }
        return json::write(list, false);
    }

    bool decodeEntries(const std::string &text, std::vector<VodEntry> &out, std::string &error) {
        out.clear();
        return json::forEachObject(text, [&out](const json::FlatObject &o) {
            VodEntry e;
            e.id = o.get("id");
            if (e.id.empty()) {
                return true;
            }
            e.name = o.get("name");
            e.categoryId = o.get("cat");
            e.poster = o.get("poster");
            e.cmd = o.get("cmd");
            e.plot = o.get("plot");
            e.director = o.get("director");
            e.cast = o.get("cast");
            e.genre = o.get("genre");
            e.year = o.get("year");
            e.rating = (float) o.getDouble("rating");
            e.minutes = (int) o.getInt("minutes");
            e.added = o.getInt("added");
            e.isSeries = o.getBool("series");
            e.episodes = splitInts(o.get("episodes"));
            out.push_back(std::move(e));
            return true;
        }, &error);
    }
}
