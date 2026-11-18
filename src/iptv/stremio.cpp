#include <algorithm>
#include <cstdlib>
#include <map>

#include "stremio.h"
#include "../core/json.h"
#include "../core/url.h"
#include "../i18n/i18n.h"

namespace stremio {

    namespace {
        const char *const MANIFEST = "/manifest.json";

        std::string lower(std::string s) {
            for (char &c: s) {
                if (c >= 'A' && c <= 'Z') {
                    c = (char) (c - 'A' + 'a');
                }
            }
            return s;
        }

        bool startsWithNoCase(const std::string &s, const char *prefix) {
            size_t n = std::char_traits<char>::length(prefix);
            return s.size() >= n && lower(s.substr(0, n)) == prefix;
        }

        // a string, or the strings of an array, joined
        std::string text(const json::Value &v, size_t max = 6) {
            if (v.isString()) {
                return v.asString();
            }
            std::string out;
            size_t n = 0;
            for (const auto &i: v.items()) {
                std::string s = i.isString() ? i.asString() : i["name"].asString();
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

        float rating(const json::Value &v) {
            double r = v.isString() ? strtod(v.asString().c_str(), nullptr) : v.asDouble(0);
            return r > 0 && r <= 10 ? (float) r : 0;
        }

        int number(const json::Value &v) {
            int64_t n = v.isString() ? strtol(v.asString().c_str(), nullptr, 10) : v.asInt(0);
            return n > 0 && n < 100000 ? (int) n : 0;
        }

        const json::Value *metasOf(const std::string &body, json::Value &root) {
            if (!json::parse(body, root) || !root.isObject() || !root["metas"].isArray()) {
                return nullptr;
            }
            return &root["metas"];
        }

        // year: "year" when given, else from releaseInfo / released
        int yearOfMeta(const json::Value &m) {
            int y = number(m["year"]);
            if (y >= 1880 && y <= 2100) {
                return y;
            }
            y = yearOf(m["releaseInfo"].asString());
            return y != 0 ? y : yearOf(m["released"].asString());
        }
    }

    std::string manifestUrl(const std::string &input, std::string &error) {
        std::string s = url::trim(input);
        if (startsWithNoCase(s, "stremio://")) {
            s = "https://" + s.substr(10);   // an "install" link
        }
        if (!startsWithNoCase(s, "http://") && !startsWithNoCase(s, "https://")) {
            error = i18n::tr("stremio.error_address");
            return "";
        }
        size_t hostStart = s.find("://") + 3;
        if (hostStart >= s.size() || s[hostStart] == '/' || s.find_first_of(" \t\r\n") != std::string::npos) {
            error = i18n::tr("stremio.error_address");
            return "";
        }
        size_t m = lower(s).find("/manifest.json");
        if (m != std::string::npos) {
            return s.substr(0, m) + MANIFEST;
        }
        size_t q = s.find_first_of("?#");
        if (q != std::string::npos) {
            s = s.substr(0, q);
        }
        while (!s.empty() && s.back() == '/') {
            s.pop_back();
        }
        return s + MANIFEST;
    }

    std::string baseOf(const std::string &manifest) {
        size_t n = std::char_traits<char>::length(MANIFEST);
        if (manifest.size() > n && lower(manifest.substr(manifest.size() - n)) == MANIFEST) {
            return manifest.substr(0, manifest.size() - n);
        }
        return manifest;
    }

    bool parseManifest(const std::string &body, Manifest &out) {
        json::Value v;
        if (!json::parse(body, v) || !v.isObject() || v["id"].asString().empty()
            || (!v["resources"].isArray() && !v["catalogs"].isArray())) {
            return false;
        }
        out = Manifest();
        out.id = v["id"].asString();
        out.name = v["name"].asString(out.id);
        out.version = v["version"].asString();
        for (const auto &r: v["resources"].items()) {
            std::string name = r.isString() ? r.asString() : r["name"].asString();
            out.meta |= name == "meta";
            out.stream |= name == "stream";
        }
        out.configurationRequired = v["behaviorHints"]["configurationRequired"].asBool(false);
        std::map<std::string, int> perType;
        for (const auto &c: v["catalogs"].items()) {
            Catalog cat;
            cat.type = c["type"].asString();
            cat.id = c["id"].asString();
            cat.name = c["name"].asString(cat.id);
            if ((cat.type != "movie" && cat.type != "series") || cat.id.empty()) {
                continue;
            }
            bool required = c["extraRequired"].isArray() && c["extraRequired"].size() > 0;
            for (const auto &e: c["extra"].items()) {
                required |= e["isRequired"].asBool(false);
                cat.pages |= e["name"].asString() == "skip";
            }
            for (const auto &e: c["extraSupported"].items()) {
                cat.pages |= e.asString() == "skip";
            }
            if (required || perType[cat.type] >= MAX_CATALOGS) {
                continue;   // a search / required value catalog lists nothing by itself
            }
            perType[cat.type]++;
            out.catalogs.push_back(cat);
        }
        return true;
    }

    std::string catalogUrl(const std::string &base, const Catalog &c, int skip) {
        return base + "/catalog/" + url::encode(c.type) + "/" + url::encode(c.id)
               + (skip > 0 ? "/skip=" + std::to_string(skip) : "") + ".json";
    }

    std::string metaUrl(const std::string &base, const std::string &type, const std::string &id) {
        return base + "/meta/" + url::encode(type) + "/" + url::encode(id) + ".json";
    }

    std::string streamUrl(const std::string &base, const std::string &type, const std::string &id) {
        return base + "/stream/" + url::encode(type) + "/" + url::encode(id) + ".json";
    }

    std::string categoryId(const Catalog &c) {
        return c.type + "/" + c.id;
    }

    bool parseMovies(const std::string &body, const std::string &category, std::vector<iptv::Movie> &out, int &count) {
        json::Value root;
        const json::Value *metas = metasOf(body, root);
        if (metas == nullptr) {
            return false;
        }
        count = (int) metas->size();
        for (const auto &m: metas->items()) {
            iptv::Movie x;
            x.streamId = m["id"].asString();
            std::string type = m["type"].asString("movie");
            if (x.streamId.empty() || type != "movie") {
                continue;
            }
            x.name = m["name"].asString(x.streamId);
            x.title = x.name;
            x.categoryId = category;
            x.icon = m["poster"].asString();
            x.rating = rating(m["imdbRating"]);
            x.year = yearOfMeta(m);
            out.push_back(std::move(x));
        }
        return true;
    }

    bool parseSeries(const std::string &body, const std::string &category, std::vector<iptv::Series> &out, int &count) {
        json::Value root;
        const json::Value *metas = metasOf(body, root);
        if (metas == nullptr) {
            return false;
        }
        count = (int) metas->size();
        for (const auto &m: metas->items()) {
            iptv::Series x;
            x.seriesId = m["id"].asString();
            std::string type = m["type"].asString("series");
            if (x.seriesId.empty() || type != "series") {
                continue;
            }
            x.name = m["name"].asString(x.seriesId);
            x.title = x.name;
            x.categoryId = category;
            x.cover = m["poster"].asString();
            x.plot = m["description"].asString();
            x.rating = rating(m["imdbRating"]);
            x.year = yearOfMeta(m);
            out.push_back(std::move(x));
        }
        return true;
    }

    bool parseMovieMeta(const std::string &body, iptv::MovieInfo &out) {
        json::Value root;
        if (!json::parse(body, root) || !root["meta"].isObject() || root["meta"]["id"].asString().empty()) {
            return false;
        }
        const json::Value &m = root["meta"];
        out.streamId = m["id"].asString();
        out.plot = m["description"].asString();
        out.genre = text(m["genres"], 4);
        out.director = text(m["director"], 2);
        out.cast = text(m["cast"], 6);
        std::string released = m["released"].asString();
        out.releaseDate = released.size() >= 10 ? released.substr(0, 10) : m["releaseInfo"].asString();
        out.coverBig = m["poster"].asString();
        out.backdrop = m["background"].asString();
        out.rating = rating(m["imdbRating"]);
        out.durationSeconds = runtimeSeconds(m["runtime"].asString());
        return true;
    }

    bool parseSeriesMeta(const std::string &body, iptv::SeriesInfo &out) {
        json::Value root;
        if (!json::parse(body, root) || !root["meta"].isObject() || root["meta"]["id"].asString().empty()) {
            return false;
        }
        const json::Value &m = root["meta"];
        iptv::Series &s = out.series;
        out.seriesId = m["id"].asString();
        s.seriesId = out.seriesId;
        s.name = m["name"].asString(out.seriesId);
        s.title = s.name;
        s.cover = m["poster"].asString();
        s.backdrop = m["background"].asString();
        s.plot = m["description"].asString();
        s.genre = text(m["genres"], 4);
        s.director = text(m["director"], 2);
        s.cast = text(m["cast"], 6);
        s.rating = rating(m["imdbRating"]);
        s.year = yearOfMeta(m);
        std::map<int, iptv::Season> seasons;
        for (const auto &v: m["videos"].items()) {
            iptv::Episode e;
            e.id = v["id"].asString();
            if (e.id.empty()) {
                continue;
            }
            e.season = v.has("season") ? std::max(0, number(v["season"])) : 1;
            if (v.has("season") && v["season"].asInt(-1) == 0) {
                e.season = 0;   // specials
            }
            e.number = number(v.has("episode") ? v["episode"] : v["number"]);
            e.title = v["title"].asString(v["name"].asString());
            e.image = v["thumbnail"].asString();
            e.plot = v["overview"].asString(v["description"].asString());
            iptv::Season &season = seasons[e.season];
            season.number = e.season;
            season.episodes.push_back(std::move(e));
        }
        out.seasons.clear();
        for (auto &kv: seasons) {
            std::stable_sort(kv.second.episodes.begin(), kv.second.episodes.end(),
                             [](const iptv::Episode &a, const iptv::Episode &b) { return a.number < b.number; });
            out.seasons.push_back(std::move(kv.second));
        }
        return true;
    }

    bool parseStreams(const std::string &body, Streams &out) {
        out = Streams();
        json::Value root;
        if (!json::parse(body, root) || !root.isObject() || !root["streams"].isArray()) {
            return false;
        }
        for (const auto &s: root["streams"].items()) {
            std::string u = s["url"].asString();
            if (!startsWithNoCase(u, "http://") && !startsWithNoCase(u, "https://")) {
                out.skipped++;   // a torrent (infoHash), a YouTube id, an external link, another scheme
                continue;
            }
            Stream st;
            st.url = u;
            st.label = s["name"].asString(s["title"].asString());
            bool otherHeaders = false;
            for (const auto &h: s["behaviorHints"]["proxyHeaders"]["request"].members()) {
                if (lower(h.first) == "user-agent") {
                    st.userAgent = h.second.asString();
                } else {
                    otherHeaders = true;
                }
            }
            if (otherHeaders) {
                out.skipped++;   // needs a Referer / Cookie... the player cannot send
                continue;
            }
            out.playable.push_back(std::move(st));
        }
        return true;
    }

    int runtimeSeconds(const std::string &runtime) {
        int total = 0;
        bool any = false;
        size_t i = 0;
        while (i < runtime.size()) {
            if (runtime[i] < '0' || runtime[i] > '9') {
                i++;
                continue;
            }
            int n = 0;
            while (i < runtime.size() && runtime[i] >= '0' && runtime[i] <= '9' && n < 100000) {
                n = n * 10 + (runtime[i] - '0');
                i++;
            }
            while (i < runtime.size() && runtime[i] == ' ') {
                i++;
            }
            char unit = i < runtime.size() ? (char) (runtime[i] | 0x20) : 'm';
            total += unit == 'h' ? n * 3600 : n * 60;   // "min", "m", or no unit: minutes
            any = true;
        }
        return any && total < 48 * 3600 ? total : 0;
    }

    int yearOf(const std::string &s) {
        for (size_t i = 0; i + 4 <= s.size(); i++) {
            bool digits = true;
            for (size_t k = 0; k < 4; k++) {
                digits &= s[i + k] >= '0' && s[i + k] <= '9';
            }
            if (digits && (i + 4 == s.size() || s[i + 4] < '0' || s[i + 4] > '9')) {
                int y = std::atoi(s.substr(i, 4).c_str());
                if (y >= 1880 && y <= 2100) {
                    return y;
                }
            }
        }
        return 0;
    }
}
