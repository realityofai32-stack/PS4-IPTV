#include <algorithm>
#include <cstdio>
#include <map>

#include "mediabrowser.h"
#include "../core/json.h"
#include "../core/url.h"

namespace mediabrowser {

    namespace {
        const int64_t TICKS_PER_SECOND = 10000000;

        std::string lower(std::string s) {
            for (char &c: s) {
                if (c >= 'A' && c <= 'Z') {
                    c = (char) (c - 'A' + 'a');
                }
            }
            return s;
        }

        // a header value: no quotes, no line breaks
        std::string headerValue(const std::string &s) {
            std::string out;
            for (char c: s) {
                if (c != '"' && c != '\r' && c != '\n') {
                    out += c;
                }
            }
            return out;
        }

        std::string joinStrings(const json::Value &arr, size_t max) {
            std::string out;
            size_t n = 0;
            for (const auto &v: arr.items()) {
                std::string s = v.asString();
                if (s.empty()) {
                    continue;
                }
                out += (out.empty() ? "" : ", ") + s;
                if (++n >= max) {
                    break;
                }
            }
            return out;
        }

        std::string primary(const json::Value &item, const std::string &base, int maxHeight) {
            std::string tag = item["ImageTags"]["Primary"].asString();
            return tag.empty() ? "" : imageUrl(base, item["Id"].asString(), "Primary", tag, maxHeight);
        }

        std::string backdrop(const json::Value &item, const std::string &base) {
            const json::Value &tags = item["BackdropImageTags"];
            std::string tag = tags.isArray() && tags.size() > 0 ? tags.at(0).asString() : "";
            return tag.empty() ? "" : imageUrl(base, item["Id"].asString(), "Backdrop", tag, 720);
        }

        float rating(const json::Value &item) {
            double r = item["CommunityRating"].asDouble(0);
            return r > 0 && r <= 10 ? (float) r : 0;
        }

        int year(const json::Value &item) {
            int64_t y = item["ProductionYear"].asInt(0);
            return y >= 1880 && y <= 2100 ? (int) y : 0;
        }

        double seconds(int64_t ticks) {
            return ticks > 0 ? (double) ticks / (double) TICKS_PER_SECOND : 0;
        }

        // "Items" of a list answer (also a bare array, as some Emby versions send for episodes)
        const json::Value *itemsOf(const json::Value &root) {
            if (root.isArray()) {
                return &root;
            }
            if (root.isObject() && root["Items"].isArray()) {
                return &root["Items"];
            }
            return nullptr;
        }

        void people(const json::Value &item, std::string &director, std::string &cast) {
            int actors = 0;
            for (const auto &p: item["People"].items()) {
                std::string type = p["Type"].asString();
                std::string name = p["Name"].asString();
                if (name.empty()) {
                    continue;
                }
                if (type == "Director" && director.empty()) {
                    director = name;
                } else if (type == "Actor" && actors < 6) {
                    cast += (cast.empty() ? "" : ", ") + name;
                    actors++;
                }
            }
        }

        std::string premiere(const json::Value &item) {
            std::string d = item["PremiereDate"].asString();
            return d.size() >= 10 ? d.substr(0, 10) : "";
        }

        int64_t daysFromCivil(int y, int m, int d) {
            y -= m <= 2;
            int64_t era = (y >= 0 ? y : y - 399) / 400;
            int64_t yoe = y - era * 400;
            int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + doe - 719468;
        }

        void flat(json::Value &v, const char *key, const std::string &s) {
            if (!s.empty()) {
                v.set(key, json::Value::makeString(s));
            }
        }
    }

    // ---------------------------------------------------------------------------------------------- requests

    std::string serverBase(const std::string &input, std::string &error) {
        url::Server s = url::normalizeServer(input);
        if (!s.ok) {
            error = s.error;
            return "";
        }
        std::string base = s.base;
        // a pasted web client address: "/web/index.html", "/web/#!/home.html" ...
        size_t web = lower(base).find("/web/");
        if (web == std::string::npos && lower(base).size() >= 4 && lower(base).compare(lower(base).size() - 4, 4, "/web") == 0) {
            web = base.size() - 4;
        }
        if (web != std::string::npos) {
            size_t scheme = base.find("://");
            if (scheme != std::string::npos && web > scheme + 3) {
                base = base.substr(0, web);
            }
        }
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        return base;
    }

    std::string deviceId(const std::string &profileId) {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c: "v7iptv:" + profileId) {
            h ^= c;
            h *= 1099511628211ull;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "v7-%016llx", (unsigned long long) h);
        return buf;
    }

    std::vector<std::string> headers(const std::string &device, const std::string &appVersion, const std::string &token) {
        std::string auth = "MediaBrowser Client=\"V7 Player\", Device=\"PlayStation 4\", DeviceId=\"" + headerValue(device)
                           + "\", Version=\"" + headerValue(appVersion) + "\"";
        if (!token.empty()) {
            auth += ", Token=\"" + headerValue(token) + "\"";
        }
        std::vector<std::string> h = {"Authorization: " + auth, "X-Emby-Authorization: " + auth,
                                      "Accept: application/json"};
        if (!token.empty()) {
            h.push_back("X-Emby-Token: " + headerValue(token));
        }
        return h;
    }

    std::string loginBody(const std::string &user, const std::string &password) {
        json::Value o = json::Value::makeObject();
        o.set("Username", json::Value::makeString(user));
        o.set("Pw", json::Value::makeString(password));
        return json::write(o, false);
    }

    std::string itemsPath(const std::string &userId, const std::string &libraryId, bool series, int start, int limit) {
        return "/Users/" + url::encode(userId) + "/Items?ParentId=" + url::encode(libraryId)
               + "&IncludeItemTypes=" + (series ? "Series" : "Movie")
               + "&Recursive=true&SortBy=SortName&SortOrder=Ascending&EnableImageTypes=Primary&ImageTypeLimit=1"
                 "&Fields=ProductionYear,CommunityRating,DateCreated,Container&EnableUserData=false"
               + "&StartIndex=" + std::to_string(start) + "&Limit=" + std::to_string(limit);
    }

    std::string streamUrl(const std::string &base, const std::string &token, const std::string &itemId) {
        std::string t = url::encode(token);
        return base + "/Videos/" + url::encode(itemId) + "/stream?static=true&api_key=" + t + "&ApiKey=" + t;
    }

    std::string imageUrl(const std::string &base, const std::string &itemId, const char *kind, const std::string &tag,
                         int maxHeight) {
        return base + "/Items/" + url::encode(itemId) + "/Images/" + kind + "?tag=" + url::encode(tag)
               + "&maxHeight=" + std::to_string(maxHeight) + "&quality=85";
    }

    const char *reportPath(Report r) {
        switch (r) {
            case Report::Start:
                return "/Sessions/Playing";
            case Report::Progress:
                return "/Sessions/Playing/Progress";
            default:
                return "/Sessions/Playing/Stopped";
        }
    }

    std::string reportBody(const std::string &itemId, const std::string &playSessionId, double positionSeconds,
                           bool paused) {
        json::Value o = json::Value::makeObject();
        o.set("ItemId", json::Value::makeString(itemId));
        o.set("PlaySessionId", json::Value::makeString(playSessionId));
        o.set("PositionTicks", json::Value::makeInt((int64_t) (std::max(0.0, positionSeconds) * TICKS_PER_SECOND)));
        o.set("IsPaused", json::Value::makeBool(paused));
        o.set("CanSeek", json::Value::makeBool(true));
        o.set("PlayMethod", json::Value::makeString("DirectStream"));
        return json::write(o, false);
    }

    // ---------------------------------------------------------------------------------------------- answers

    bool parsePublicInfo(const std::string &body, ServerInfo &out) {
        json::Value v;
        if (!json::parse(body, v) || !v.isObject()) {
            return false;
        }
        out.id = v["Id"].asString();
        out.version = v["Version"].asString();
        out.name = v["ServerName"].asString();
        out.product = v["ProductName"].asString();
        if (out.id.empty() || out.version.empty()) {
            return false;
        }
        if (out.product.empty()) {
            out.product = "Emby Server";   // Emby's public information has no product name
        }
        return true;
    }

    bool parseLogin(const std::string &body, Login &out) {
        json::Value v;
        if (!json::parse(body, v) || !v.isObject()) {
            return false;
        }
        out.token = v["AccessToken"].asString();
        out.userId = v["User"]["Id"].asString();
        out.userName = v["User"]["Name"].asString();
        out.serverId = v["ServerId"].asString();
        return !out.token.empty() && !out.userId.empty();
    }

    bool parseViews(const std::string &body, std::vector<Library> &out) {
        out.clear();
        json::Value v;
        if (!json::parse(body, v)) {
            return false;
        }
        const json::Value *items = itemsOf(v);
        if (items == nullptr) {
            return false;
        }
        for (const auto &i: items->items()) {
            Library l;
            l.id = i["Id"].asString();
            l.name = i["Name"].asString();
            l.type = lower(i["CollectionType"].asString());
            if (!l.id.empty()) {
                out.push_back(l);
            }
        }
        return true;
    }

    bool parseMovies(const std::string &body, const std::string &base, const std::string &categoryId,
                     std::vector<iptv::Movie> &out, Page &page) {
        json::Value v;
        if (!json::parse(body, v)) {
            return false;
        }
        const json::Value *items = itemsOf(v);
        if (items == nullptr) {
            return false;
        }
        page.items = (int) items->size();
        page.total = (int) v["TotalRecordCount"].asInt(page.items);
        for (const auto &i: items->items()) {
            iptv::Movie m;
            m.streamId = i["Id"].asString();
            if (m.streamId.empty() || i["Type"].asString("Movie") != "Movie") {
                continue;
            }
            m.name = i["Name"].asString();
            if (m.name.empty()) {
                m.name = "Movie";   // i18n-exempt: never shown in practice (servers name every item)
            }
            m.title = m.name;
            m.categoryId = categoryId;
            m.icon = primary(i, base, 450);
            m.extension = containerOf(i["Container"].asString());
            m.rating = rating(i);
            m.year = year(i);
            m.added = parseDate(i["DateCreated"].asString());
            out.push_back(std::move(m));
        }
        return true;
    }

    bool parseSeries(const std::string &body, const std::string &base, const std::string &categoryId,
                     std::vector<iptv::Series> &out, Page &page) {
        json::Value v;
        if (!json::parse(body, v)) {
            return false;
        }
        const json::Value *items = itemsOf(v);
        if (items == nullptr) {
            return false;
        }
        page.items = (int) items->size();
        page.total = (int) v["TotalRecordCount"].asInt(page.items);
        for (const auto &i: items->items()) {
            iptv::Series s;
            s.seriesId = i["Id"].asString();
            if (s.seriesId.empty() || i["Type"].asString("Series") != "Series") {
                continue;
            }
            s.name = i["Name"].asString();
            s.title = s.name;
            s.categoryId = categoryId;
            s.cover = primary(i, base, 450);
            s.rating = rating(i);
            s.year = year(i);
            s.lastModified = parseDate(i["DateCreated"].asString());
            out.push_back(std::move(s));
        }
        return true;
    }

    bool parseMovieInfo(const std::string &body, const std::string &base, iptv::MovieInfo &out) {
        json::Value i;
        if (!json::parse(body, i) || !i.isObject() || i["Id"].asString().empty()) {
            return false;
        }
        out.streamId = i["Id"].asString();
        out.plot = i["Overview"].asString();
        out.genre = joinStrings(i["Genres"], 4);
        people(i, out.director, out.cast);
        out.releaseDate = premiere(i);
        out.coverBig = primary(i, base, 720);
        out.backdrop = backdrop(i, base);
        out.tmdbId = i["ProviderIds"]["Tmdb"].asString();
        out.rating = rating(i);
        out.durationSeconds = (int) seconds(i["RunTimeTicks"].asInt(0));
        out.resumeSeconds = seconds(i["UserData"]["PlaybackPositionTicks"].asInt(0));
        const json::Value &sources = i["MediaSources"];
        if (sources.isArray() && sources.size() > 0) {
            const json::Value &src = sources.at(0);
            out.media.bitrateKbps = (int) (src["Bitrate"].asInt(0) / 1000);
            for (const auto &st: src["MediaStreams"].items()) {
                std::string type = st["Type"].asString();
                if (type == "Video" && out.media.width == 0) {
                    out.media.width = (int) st["Width"].asInt(0);
                    out.media.height = (int) st["Height"].asInt(0);
                    out.media.videoCodec = lower(st["Codec"].asString());
                } else if (type == "Audio" && out.media.audioCodec.empty()) {
                    out.media.audioCodec = lower(st["Codec"].asString());
                    out.media.audioChannels = (int) st["Channels"].asInt(0);
                    out.media.audioLanguage = st["Language"].asString();
                }
            }
        }
        return true;
    }

    bool parseSeriesDetail(const std::string &body, const std::string &base, iptv::Series &out) {
        json::Value i;
        if (!json::parse(body, i) || !i.isObject() || i["Id"].asString().empty()) {
            return false;
        }
        if (out.seriesId.empty()) {
            out.seriesId = i["Id"].asString();
        }
        if (out.name.empty()) {
            out.name = i["Name"].asString();
            out.title = out.name;
        }
        out.plot = i["Overview"].asString();
        out.genre = joinStrings(i["Genres"], 4);
        people(i, out.director, out.cast);
        out.releaseDate = premiere(i);
        if (out.cover.empty()) {
            out.cover = primary(i, base, 450);
        }
        out.backdrop = backdrop(i, base);
        if (out.rating <= 0) {
            out.rating = rating(i);
        }
        if (out.year == 0) {
            out.year = year(i);
        }
        int64_t ticks = i["RunTimeTicks"].asInt(0);
        out.runtimeMinutes = ticks > 0 ? (int) (seconds(ticks) / 60) : 0;
        return true;
    }

    bool parseEpisodes(const std::string &body, const std::string &base, iptv::SeriesInfo &out) {
        json::Value v;
        if (!json::parse(body, v)) {
            return false;
        }
        const json::Value *items = itemsOf(v);
        if (items == nullptr) {
            return false;
        }
        std::map<int, iptv::Season> seasons;
        for (const auto &i: items->items()) {
            if (i["Id"].asString().empty() || i["LocationType"].asString() == "Virtual") {
                continue;   // a missing episode the server only knows about: nothing to play
            }
            iptv::Episode e;
            e.id = i["Id"].asString();
            e.season = (int) std::max<int64_t>(0, i["ParentIndexNumber"].asInt(0));
            e.number = (int) std::max<int64_t>(0, i["IndexNumber"].asInt(0));
            e.title = i["Name"].asString();
            e.extension = containerOf(i["Container"].asString());
            e.image = primary(i, base, 360);
            e.plot = i["Overview"].asString();
            e.durationSeconds = (int) seconds(i["RunTimeTicks"].asInt(0));
            e.rating = rating(i);
            e.resumeSeconds = seconds(i["UserData"]["PlaybackPositionTicks"].asInt(0));
            iptv::Season &s = seasons[e.season];
            s.number = e.season;
            if (s.name.empty()) {
                s.name = i["SeasonName"].asString();
            }
            s.episodes.push_back(std::move(e));
        }
        out.seasons.clear();
        for (auto &kv: seasons) {
            std::stable_sort(kv.second.episodes.begin(), kv.second.episodes.end(),
                             [](const iptv::Episode &a, const iptv::Episode &b) { return a.number < b.number; });
            out.seasons.push_back(std::move(kv.second));
        }
        return true;
    }

    // ---------------------------------------------------------------------------------------------- values

    int64_t parseDate(const std::string &iso) {
        int y, mo, d, h = 0, mi = 0, s = 0;
        if (iso.size() < 10 || sscanf(iso.c_str(), "%4d-%2d-%2d", &y, &mo, &d) != 3) {
            return 0;
        }
        if (iso.size() >= 19 && (iso[10] == 'T' || iso[10] == ' ')) {
            sscanf(iso.c_str() + 11, "%2d:%2d:%2d", &h, &mi, &s);
        }
        if (y < 1970 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) {
            return 0;
        }
        int64_t t = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
        // a zone after the time ("+03:00"); "Z" / none: UTC
        size_t zone = iso.find_first_of("+-", 19);
        if (zone != std::string::npos && iso.size() >= zone + 6) {
            int zh = 0, zm = 0;
            if (sscanf(iso.c_str() + zone + 1, "%2d:%2d", &zh, &zm) == 2) {
                t -= (iso[zone] == '-' ? -1 : 1) * (zh * 3600 + zm * 60);
            }
        }
        return t;
    }

    std::string containerOf(const std::string &raw) {
        std::string l = lower(raw);
        if (l.empty()) {
            return "mkv";
        }
        if (l.find(',') == std::string::npos) {
            return l;
        }
        if (l.find("mp4") != std::string::npos) {
            return "mp4";
        }
        return l.substr(0, l.find(','));
    }

    // ---------------------------------------------------------------------------------------------- saved copy

    std::string encodeMovies(const std::vector<iptv::Category> &categories, const std::vector<iptv::Movie> &movies) {
        json::Value arr = json::Value::makeArray();
        for (const auto &c: categories) {
            json::Value o = json::Value::makeObject();
            o.set("k", json::Value::makeString("c"));
            o.set("i", json::Value::makeString(c.id));
            o.set("n", json::Value::makeString(c.name));
            arr.push(std::move(o));
        }
        for (const auto &m: movies) {
            json::Value o = json::Value::makeObject();
            o.set("k", json::Value::makeString("m"));
            o.set("i", json::Value::makeString(m.streamId));
            o.set("n", json::Value::makeString(m.name));
            o.set("c", json::Value::makeString(m.categoryId));
            flat(o, "p", m.icon);
            flat(o, "x", m.extension);
            o.set("r", json::Value::makeNumber(m.rating));
            o.set("y", json::Value::makeInt(m.year));
            o.set("a", json::Value::makeInt(m.added));
            arr.push(std::move(o));
        }
        return json::write(arr, false);
    }

    bool decodeMovies(const std::string &text, std::vector<iptv::Category> &categories, std::vector<iptv::Movie> &movies) {
        categories.clear();
        movies.clear();
        std::string err;
        return json::forEachObject(text, [&](const json::FlatObject &o) {
            if (o.get("k") == "c") {
                categories.push_back({o.get("i"), o.get("n"), ""});
            } else if (o.get("k") == "m" && !o.get("i").empty()) {
                iptv::Movie m;
                m.streamId = o.get("i");
                m.name = o.get("n");
                m.title = m.name;
                m.categoryId = o.get("c");
                m.icon = o.get("p");
                m.extension = o.get("x").empty() ? "mkv" : o.get("x");
                m.rating = (float) o.getDouble("r", 0);
                m.year = (int) o.getInt("y", 0);
                m.added = o.getInt("a", 0);
                movies.push_back(std::move(m));
            }
            return true;
        }, &err) && !categories.empty();
    }

    std::string encodeSeries(const std::vector<iptv::Category> &categories, const std::vector<iptv::Series> &series) {
        json::Value arr = json::Value::makeArray();
        for (const auto &c: categories) {
            json::Value o = json::Value::makeObject();
            o.set("k", json::Value::makeString("c"));
            o.set("i", json::Value::makeString(c.id));
            o.set("n", json::Value::makeString(c.name));
            arr.push(std::move(o));
        }
        for (const auto &s: series) {
            json::Value o = json::Value::makeObject();
            o.set("k", json::Value::makeString("s"));
            o.set("i", json::Value::makeString(s.seriesId));
            o.set("n", json::Value::makeString(s.name));
            o.set("c", json::Value::makeString(s.categoryId));
            flat(o, "p", s.cover);
            o.set("r", json::Value::makeNumber(s.rating));
            o.set("y", json::Value::makeInt(s.year));
            o.set("a", json::Value::makeInt(s.lastModified));
            arr.push(std::move(o));
        }
        return json::write(arr, false);
    }

    bool decodeSeries(const std::string &text, std::vector<iptv::Category> &categories, std::vector<iptv::Series> &series) {
        categories.clear();
        series.clear();
        std::string err;
        return json::forEachObject(text, [&](const json::FlatObject &o) {
            if (o.get("k") == "c") {
                categories.push_back({o.get("i"), o.get("n"), ""});
            } else if (o.get("k") == "s" && !o.get("i").empty()) {
                iptv::Series s;
                s.seriesId = o.get("i");
                s.name = o.get("n");
                s.title = s.name;
                s.categoryId = o.get("c");
                s.cover = o.get("p");
                s.rating = (float) o.getDouble("r", 0);
                s.year = (int) o.getInt("y", 0);
                s.lastModified = o.getInt("a", 0);
                series.push_back(std::move(s));
            }
            return true;
        }, &err) && !categories.empty();
    }
}
