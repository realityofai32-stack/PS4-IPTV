#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <unordered_set>

#include "xtream.h"
#include "../core/json.h"
#include "../core/url.h"

namespace iptv {
    const char *contentTypeName(ContentType type) {
        switch (type) {
            case ContentType::Live:
                return "Live TV";
            case ContentType::Movie:
                return "Movies";
            default:
                return "Series";
        }
    }
}

namespace xtream {

    using namespace iptv;

    namespace {
        std::string mediaUrl(const Profile &p, const char *kind, const std::string &id, const std::string &ext) {
            std::string e = ext.empty() ? "ts" : ext;
            if (!e.empty() && e[0] == '.') {
                e = e.substr(1);
            }
            return p.server + "/" + kind + "/" + url::encode(p.username) + "/" + url::encode(p.password) + "/"
                   + url::encode(id) + "." + url::encode(e);
        }
    }

    std::string apiUrl(const Profile &p, const std::string &action, const std::string &extra) {
        std::string u = p.server + "/player_api.php?username=" + url::encode(p.username) + "&password="
                        + url::encode(p.password);
        if (!action.empty()) {
            u += "&action=" + url::encode(action);
        }
        if (!extra.empty()) {
            u += "&" + extra;
        }
        return u;
    }

    std::string liveUrl(const Profile &p, const std::string &streamId, const std::string &ext) {
        return mediaUrl(p, "live", streamId, ext);
    }

    std::string movieUrl(const Profile &p, const std::string &streamId, const std::string &ext) {
        return mediaUrl(p, "movie", streamId, ext);
    }

    std::string seriesUrl(const Profile &p, const std::string &episodeId, const std::string &ext) {
        return mediaUrl(p, "series", episodeId, ext);
    }

    AuthResult parseAuth(long httpStatus, const std::string &body, int64_t now) {
        AuthResult r;
        if (httpStatus == 401) {
            r.status = AuthStatus::InvalidCredentials;
            r.detail = "HTTP 401";
            return r;
        }
        if (httpStatus == 403) {
            r.status = AuthStatus::Refused;
            r.detail = "HTTP 403";
            return r;
        }
        if (httpStatus >= 500) {
            r.status = AuthStatus::ServerError;
            r.detail = "HTTP " + std::to_string(httpStatus);
            return r;
        }
        if (httpStatus != 200) {
            r.status = AuthStatus::ServerError;
            r.detail = "unexpected HTTP " + std::to_string(httpStatus);
            return r;
        }

        json::Value root;
        std::string err;
        if (!json::parse(body, root, &err)) {
            r.status = AuthStatus::Malformed;
            r.detail = "response is not JSON (" + err + ", " + std::to_string(body.size()) + " bytes)";
            return r;
        }
        if (root.isArray() && root.size() == 0) {
            // several panels answer wrong credentials with an empty array
            r.status = AuthStatus::InvalidCredentials;
            r.detail = "empty array response";
            return r;
        }
        const json::Value &ui = root["user_info"];
        if (!ui.isObject()) {
            r.status = AuthStatus::Malformed;
            r.detail = "no user_info object";
            return r;
        }

        AccountInfo &a = r.account;
        a.status = ui["status"].asString();
        a.message = ui["message"].asString();
        a.expiresAt = ui["exp_date"].asInt(0);
        a.trial = ui["is_trial"].asBool(false);
        a.maxConnections = (int) ui["max_connections"].asInt(0);
        a.activeConnections = (int) ui["active_cons"].asInt(0);
        for (const auto &f: ui["allowed_output_formats"].items()) {
            a.outputFormats.push_back(f.asString());
        }
        const json::Value &si = root["server_info"];
        a.serverUrl = si["url"].asString();
        a.serverPort = si["port"].asString();
        a.serverHttpsPort = si["https_port"].asString();
        a.serverProtocol = si["server_protocol"].asString();
        a.timezone = si["timezone"].asString();
        a.serverTime = si["timestamp_now"].asInt(0);

        if (!ui["auth"].asBool(false)) {
            r.status = AuthStatus::InvalidCredentials;
            r.detail = "user_info.auth = 0";
            return r;
        }
        std::string st = url::lower(a.status);
        if (st == "expired" || (a.expiresAt > 0 && now > 0 && a.expiresAt < now)) {
            r.status = AuthStatus::Expired;
        } else if (st == "banned") {
            r.status = AuthStatus::Banned;
        } else if (st == "disabled") {
            r.status = AuthStatus::Disabled;
        } else {
            r.status = AuthStatus::Ok;
        }
        r.detail = "status '" + a.status + "'";
        return r;
    }

    bool parseCategories(const std::string &body, std::vector<Category> &out, std::string &error) {
        out.clear();
        json::Value root;
        if (!json::parse(body, root, &error)) {
            error = "response is not JSON: " + error;
            return false;
        }
        if (!root.isArray()) {
            error = root.isObject() ? "unexpected object response (not a category list)" : "unexpected response";
            return false;
        }
        out.reserve(root.size());
        for (const auto &item: root.items()) {
            if (!item.isObject()) {
                continue;
            }
            Category c;
            c.id = item["category_id"].asString();
            c.name = item["category_name"].asString();
            c.parentId = item["parent_id"].asString();
            if (c.id.empty()) {
                continue;  // malformed entry: skip, never crash
            }
            if (c.name.empty()) {
                c.name = "Category " + c.id;
            }
            out.push_back(std::move(c));
        }
        return true;
    }

    namespace {
        // bookkeeping shared by the list parsers: rejected entries by reason, duplicate ids
        struct ListAudit {
            ParseStats stats;
            std::unordered_set<std::string> ids;

            // false: the entry is rejected (and counted)
            bool accept(const std::string &id) {
                if (id.empty()) {
                    stats.rejectedMissingId++;
                    return false;
                }
                if (!ids.insert(id).second) {
                    stats.rejectedDuplicateId++;
                    return false;
                }
                return true;
            }

            void finish(size_t elements, size_t kept, ParseStats *out) {
                stats.raw = (int) elements;
                stats.parsed = (int) kept;
                stats.rejectedNotObject = stats.raw - stats.parsed - stats.rejectedMissingId - stats.rejectedDuplicateId;
                if (out) {
                    *out = stats;
                }
            }
        };
    }

    bool parseLiveStreams(const std::string &body, std::vector<LiveChannel> &out, std::string &error,
                          ParseStats *stats) {
        out.clear();
        ListAudit audit;
        size_t elements = 0;
        bool ok = json::forEachObject(body, [&out, &audit](const json::FlatObject &o) {
            LiveChannel c;
            c.streamId = url::trim(o.get("stream_id"));
            if (!audit.accept(c.streamId)) {
                return true;  // malformed entry: skip
            }
            c.name = url::trim(o.get("name"));
            if (c.name.empty()) {
                audit.stats.missingName++;
                c.name = "Channel " + c.streamId;
            }
            c.categoryId = url::trim(o.get("category_id"));
            audit.stats.missingCategory += c.categoryId.empty();
            c.icon = url::trim(o.get("stream_icon"));
            audit.stats.missingPoster += c.icon.empty();
            c.epgId = o.get("epg_channel_id");
            c.added = o.getInt("added", 0);
            c.num = (int) o.getInt("num", 0);
            c.archive = o.getBool("tv_archive", false);
            out.push_back(std::move(c));
            return true;
        }, &error, &elements);
        if (!ok) {
            error = "response is not a channel list: " + error;
        }
        audit.finish(elements, out.size(), stats);
        return ok;
    }

    // ------------------------------------------------------------------ Movies / Series

    namespace {
        bool isDigit(char c) {
            return c >= '0' && c <= '9';
        }

        int yearFromDate(const std::string &date) {
            // "2026-09-20", "2019", "20.09.2019"
            for (size_t i = 0; i + 4 <= date.size(); i++) {
                if (isDigit(date[i]) && isDigit(date[i + 1]) && isDigit(date[i + 2]) && isDigit(date[i + 3])
                    && (i + 4 == date.size() || !isDigit(date[i + 4])) && (i == 0 || !isDigit(date[i - 1]))) {
                    int y = std::stoi(date.substr(i, 4));
                    if (y >= 1900 && y <= 2099) {
                        return y;
                    }
                }
            }
            return 0;
        }

        float clampRating(double r) {
            return r > 0 && r <= 10 ? (float) r : 0.0f;
        }

        void readMedia(const json::Value &info, MediaSummary &m) {
            const json::Value &v = info["video"];
            if (v.isObject()) {
                m.width = (int) v["width"].asInt(0);
                m.height = (int) v["height"].asInt(0);
                m.videoCodec = v["codec_name"].asString();
            }
            const json::Value &a = info["audio"];
            if (a.isObject()) {
                m.audioCodec = a["codec_name"].asString();
                m.audioChannels = (int) a["channels"].asInt(0);
                m.audioLanguage = a["tags"]["language"].asString();
            }
            m.bitrateKbps = (int) info["bitrate"].asInt(0);
        }

        int durationOf(const json::Value &info) {
            int64_t secs = info["duration_secs"].asInt(0);
            if (secs > 0) {
                return (int) secs;
            }
            // "02:25:00"
            std::string d = info["duration"].asString();
            int h = 0, mi = 0, s = 0;
            if (sscanf(d.c_str(), "%d:%d:%d", &h, &mi, &s) == 3) {
                return h * 3600 + mi * 60 + s;
            }
            return 0;
        }

        std::string firstString(const json::Value &v) {
            // backdrop_path: string or array of strings
            if (v.isArray()) {
                return v.size() > 0 ? url::trim(v.at(0).asString()) : "";
            }
            return url::trim(v.asString());
        }

        void readSeriesFields(const json::Value &o, Series &s) {
            s.name = url::trim(o["name"].asString(s.name));
            s.cover = url::trim(o["cover"].asString(s.cover));
            s.plot = cleanText(o["plot"].asString());
            s.cast = cleanText(o["cast"].asString());
            s.director = cleanText(o["director"].asString());
            s.genre = cleanText(o["genre"].asString());
            s.releaseDate = cleanText(o["releaseDate"].asString(o["release_date"].asString()));
            s.rating = clampRating(o["rating"].asDouble(0));
            s.runtimeMinutes = (int) o["episode_run_time"].asInt(0);
            s.lastModified = o["last_modified"].asInt(0);
            if (!o["category_id"].asString().empty()) {
                s.categoryId = o["category_id"].asString();
            }
        }

        void finishSeries(Series &s) {
            if (s.name.empty()) {
                s.name = "Series " + s.seriesId;
            }
            int fromName = 0;
            splitTitleYear(s.name, s.title, fromName);
            s.year = yearFromDate(s.releaseDate);
            if (s.year == 0) {
                s.year = fromName;
            }
        }
    }

    std::string channelNameWithoutPrefix(const std::string &name) {
        std::string n = url::trim(name);
        size_t i = 0;
        bool bars = !n.empty() && n[0] == '|';
        if (bars) {
            i = 1;
        }
        size_t start = i;
        while (i < n.size() && i - start < 5 && isalnum((unsigned char) n[i])) {
            i++;
        }
        size_t len = i - start;
        if (len < 1 || len > 4 || i >= n.size()) {
            return "";
        }
        if (bars ? n[i] != '|' : (n[i] != ':' && n[i] != '|')) {
            return "";
        }
        std::string rest = url::trim(n.substr(i + 1));
        while (!rest.empty() && (rest[0] == ':' || rest[0] == '|' || rest[0] == '-')) {
            rest = url::trim(rest.substr(1));
        }
        return rest;
    }

    std::string cleanText(const std::string &s) {
        std::string t = url::trim(s);
        std::string l = url::lower(t);
        if (l == "-" || l == "n/a" || l == "null" || l == "none" || l == "--") {
            return "";
        }
        return t;
    }

    void splitTitleYear(const std::string &name, std::string &title, int &year) {
        title = url::trim(name);
        year = 0;
        std::string t = title;
        bool paren = !t.empty() && t.back() == ')';
        if (paren) {
            t.pop_back();
        }
        if (t.size() < 6) {
            return;
        }
        std::string tail = t.substr(t.size() - 4);
        if (!isDigit(tail[0]) || !isDigit(tail[1]) || !isDigit(tail[2]) || !isDigit(tail[3])) {
            return;
        }
        int y = std::stoi(tail);
        if (y < 1900 || y > 2099) {
            return;
        }
        std::string rest = t.substr(0, t.size() - 4);
        if (paren) {
            if (rest.empty() || rest.back() != '(') {
                return;
            }
            rest.pop_back();
        } else if (rest.back() != ' ' && rest.back() != '-' && rest.back() != '.') {
            return;   // "Blade Runner2049" style: digits glued to a word are part of the title
        }
        while (!rest.empty() && (rest.back() == ' ' || rest.back() == '-' || rest.back() == '.')) {
            rest.pop_back();
        }
        if (rest.empty()) {
            return;   // the name is only a year
        }
        title = rest;
        year = y;
    }

    std::string cleanEpisodeTitle(const std::string &raw, const std::string &seriesName) {
        std::string t = url::trim(raw);
        // remove "S01-E01" / "S01E01" / "s1 e1"
        std::string l = url::lower(t);
        for (size_t i = 0; i < l.size(); i++) {
            if (l[i] != 's' || i + 1 >= l.size() || !isDigit(l[i + 1]) || (i > 0 && isalnum((unsigned char) l[i - 1]))) {
                continue;
            }
            size_t j = i + 1;
            while (j < l.size() && isDigit(l[j])) {
                j++;
            }
            size_t k = j;
            while (k < l.size() && (l[k] == '-' || l[k] == ' ' || l[k] == '.' || l[k] == '_')) {
                k++;
            }
            if (k < l.size() && l[k] == 'e' && k + 1 < l.size() && isDigit(l[k + 1])) {
                size_t e = k + 1;
                while (e < l.size() && isDigit(l[e])) {
                    e++;
                }
                t.erase(i, e - i);
                l.erase(i, e - i);
                break;
            }
        }
        // remove the series name at the start, ignoring punctuation and spaces ("Handmaids" vs "Handmaid's")
        std::string loose;
        for (char c: url::lower(seriesName)) {
            if (isalnum((unsigned char) c) || (unsigned char) c >= 0x80) {
                loose += c;
            }
        }
        size_t matched = 0;
        size_t cut = 0;
        for (size_t i = 0; i < t.size() && matched < loose.size(); i++) {
            char c = (char) tolower((unsigned char) t[i]);
            if (!(isalnum((unsigned char) c) || (unsigned char) c >= 0x80)) {
                continue;
            }
            if (c != loose[matched]) {
                break;
            }
            matched++;
            cut = i + 1;
        }
        bool wordEnds = cut >= t.size() || !isalnum((unsigned char) t[cut]);   // "Showtime" is not "Show"
        if (!loose.empty() && matched == loose.size() && wordEnds) {
            t.erase(0, cut);
        }
        // the removed episode code may leave double spaces
        std::string collapsed;
        for (char c: t) {
            if (!(c == ' ' && !collapsed.empty() && collapsed.back() == ' ')) {
                collapsed += c;
            }
        }
        t = collapsed;
        auto sep = [](char c) { return c == ' ' || c == '-' || c == ':' || c == '|' || c == '.' || c == '_'; };
        while (!t.empty() && sep(t.front())) {
            t.erase(t.begin());
        }
        while (!t.empty() && sep(t.back())) {
            t.pop_back();
        }
        return t;
    }

    bool parseVodStreams(const std::string &body, std::vector<Movie> &out, std::string &error, ParseStats *stats) {
        out.clear();
        ListAudit audit;
        size_t elements = 0;
        bool ok = json::forEachObject(body, [&out, &audit](const json::FlatObject &o) {
            Movie m;
            m.streamId = url::trim(o.get("stream_id"));
            if (!audit.accept(m.streamId)) {
                return true;  // no identity: cannot be played or remembered
            }
            m.name = url::trim(o.get("name"));
            if (m.name.empty()) {
                audit.stats.missingName++;
                m.name = "Movie " + m.streamId;
            }
            splitTitleYear(m.name, m.title, m.year);
            audit.stats.titleYearAliases += m.year > 0;
            m.categoryId = url::trim(o.get("category_id"));
            audit.stats.missingCategory += m.categoryId.empty();
            m.icon = url::trim(o.get("stream_icon"));
            audit.stats.missingPoster += m.icon.empty();
            m.extension = url::trim(o.get("container_extension"));
            audit.stats.missingExtension += m.extension.empty();
            m.rating = clampRating(o.getDouble("rating", 0));
            m.added = o.getInt("added", 0);
            out.push_back(std::move(m));
            return true;
        }, &error, &elements);
        if (!ok) {
            error = "response is not a movie list: " + error;
        }
        audit.finish(elements, out.size(), stats);
        return ok;
    }

    bool parseSeriesList(const std::string &body, std::vector<Series> &out, std::string &error, ParseStats *stats) {
        out.clear();
        ListAudit audit;
        size_t elements = 0;
        bool ok = json::forEachObject(body, [&out, &audit](const json::FlatObject &o) {
            Series s;
            s.seriesId = url::trim(o.get("series_id"));
            if (!audit.accept(s.seriesId)) {
                return true;
            }
            s.name = url::trim(o.get("name"));
            audit.stats.missingName += s.name.empty();
            s.categoryId = url::trim(o.get("category_id"));
            audit.stats.missingCategory += s.categoryId.empty();
            s.cover = url::trim(o.get("cover"));
            audit.stats.missingPoster += s.cover.empty();
            s.plot = cleanText(o.get("plot"));
            s.cast = cleanText(o.get("cast"));
            s.director = cleanText(o.get("director"));
            s.genre = cleanText(o.get("genre"));
            s.releaseDate = cleanText(o.has("releaseDate") ? o.get("releaseDate") : o.get("release_date"));
            s.rating = clampRating(o.getDouble("rating", 0));
            s.runtimeMinutes = (int) o.getInt("episode_run_time", 0);
            s.lastModified = o.getInt("last_modified", 0);
            finishSeries(s);
            audit.stats.titleYearAliases += s.title != s.name;
            out.push_back(std::move(s));
            return true;
        }, &error, &elements);
        if (!ok) {
            error = "response is not a series list: " + error;
        }
        audit.finish(elements, out.size(), stats);
        return ok;
    }

    bool parseVodInfo(const std::string &body, MovieInfo &out, std::string &error) {
        out = MovieInfo();
        json::Value root;
        if (!json::parse(body, root, &error)) {
            error = "response is not JSON: " + error;
            return false;
        }
        if (!root.isObject()) {
            error = "unexpected movie info response";
            return false;
        }
        out.streamId = root["movie_data"]["stream_id"].asString();
        const json::Value &info = root["info"];   // some panels send [] when there is no info
        if (!info.isObject()) {
            return true;
        }
        out.plot = cleanText(info["plot"].asString(info["description"].asString()));
        out.genre = cleanText(info["genre"].asString());
        out.director = cleanText(info["director"].asString());
        out.cast = cleanText(info["cast"].asString(info["actors"].asString()));
        out.releaseDate = cleanText(info["releasedate"].asString(info["release_date"].asString()));
        out.coverBig = url::trim(info["cover_big"].asString(info["movie_image"].asString()));
        out.backdrop = firstString(info["backdrop_path"]);
        out.tmdbId = info["tmdb_id"].asString();
        out.rating = clampRating(info["rating"].asDouble(0));
        out.durationSeconds = durationOf(info);
        readMedia(info, out.media);
        return true;
    }

    bool parseSeriesInfo(const std::string &body, SeriesInfo &out, std::string &error) {
        out = SeriesInfo();
        json::Value root;
        if (!json::parse(body, root, &error)) {
            error = "response is not JSON: " + error;
            return false;
        }
        if (!root.isObject()) {
            error = "unexpected series info response";
            return false;
        }
        const json::Value &info = root["info"];
        if (info.isObject()) {
            readSeriesFields(info, out.series);
        }
        // episodes: {"1": [...], "2": [...]} or [[...], [...]] or a flat [...]
        std::vector<const json::Value *> episodes;
        const json::Value &eps = root["episodes"];
        auto collect = [&episodes](const json::Value &list) {
            if (list.isArray()) {
                for (const auto &e: list.items()) {
                    if (e.isObject()) {
                        episodes.push_back(&e);
                    } else if (e.isArray()) {
                        for (const auto &x: e.items()) {
                            if (x.isObject()) {
                                episodes.push_back(&x);
                            }
                        }
                    }
                }
            }
        };
        if (eps.isObject()) {
            for (const auto &m: eps.members()) {
                collect(m.second);
            }
        } else {
            collect(eps);
        }
        std::map<int, Season> seasons;
        for (const json::Value *pe: episodes) {
            const json::Value &e = *pe;
            Episode ep;
            ep.id = e["id"].asString();
            if (ep.id.empty()) {
                continue;
            }
            ep.season = (int) e["season"].asInt(0);
            ep.number = (int) e["episode_num"].asInt(0);
            ep.extension = url::trim(e["container_extension"].asString());
            ep.title = cleanEpisodeTitle(e["title"].asString(), out.series.name);
            const json::Value &ei = e["info"];
            if (ei.isObject()) {
                ep.image = url::trim(ei["movie_image"].asString());
                ep.plot = cleanText(ei["plot"].asString());
                ep.durationSeconds = durationOf(ei);
                ep.rating = clampRating(ei["rating"].asDouble(0));
                readMedia(ei, ep.media);
            }
            seasons[ep.season].episodes.push_back(std::move(ep));
        }
        for (const auto &s: root["seasons"].items()) {
            int n = (int) s["season_number"].asInt(-1);
            auto it = seasons.find(n);
            if (it != seasons.end()) {
                it->second.name = cleanText(s["name"].asString());
                it->second.cover = url::trim(s["cover"].asString());
            }
        }
        for (auto &s: seasons) {
            s.second.number = s.first;
            std::stable_sort(s.second.episodes.begin(), s.second.episodes.end(),
                             [](const Episode &a, const Episode &b) { return a.number < b.number; });
            out.seasons.push_back(std::move(s.second));
        }
        finishSeries(out.series);
        return true;
    }

    std::string authStatusText(AuthStatus status) {
        switch (status) {
            case AuthStatus::Ok:
                return "Connected";
            case AuthStatus::InvalidCredentials:
                return "Invalid Xtream username or password";
            case AuthStatus::Expired:
                return "Subscription expired";
            case AuthStatus::Banned:
                return "Account banned by the provider";
            case AuthStatus::Disabled:
                return "Account disabled by the provider";
            case AuthStatus::Refused:
                return "The server refused access (HTTP 403)";
            case AuthStatus::ServerError:
                return "The IPTV server returned an error";
            case AuthStatus::Malformed:
                return "The server did not answer like an Xtream server";
            default:
                return "Unable to connect to the server";
        }
    }
}
