#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "download_model.h"
#include "../core/json.h"
#include "../i18n/i18n.h"

namespace dl {

    namespace {
        const int MANIFEST_VERSION = 1;

        const char *KIND_KEYS[] = {"movie", "episode"};
        const char *STATE_KEYS[] = {"queued", "downloading", "paused", "completed", "failed", "waiting", "cancelled"};
        const char *PROBLEM_KEYS[] = {"", "network", "http_auth", "http_forbidden", "http_not_found", "http_server",
                                      "no_space", "storage", "file_too_large", "size_mismatch", "restart_needed",
                                      "profile_missing", "file_missing"};

        bool parseInt64(const std::string &s, size_t &pos, int64_t &out) {
            size_t start = pos;
            int64_t v = 0;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
                if (v > (INT64_MAX - 9) / 10) {
                    return false;
                }
                v = v * 10 + (s[pos] - '0');
                pos++;
            }
            out = v;
            return pos > start;
        }

        void skipSpaces(const std::string &s, size_t &pos) {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) {
                pos++;
            }
        }
    }

    // ------------------------------------------------------------------ identity / file names

    std::string makeKey(const std::string &profileId, Kind kind, const std::string &contentId) {
        return profileId + "|" + KIND_KEYS[(int) kind] + "|" + contentId;
    }

    std::string sanitizeComponent(const std::string &s) {
        std::string out;
        for (char c: s) {
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            out += ok ? c : '_';
            if (out.size() >= 48) {
                break;
            }
        }
        return out;
    }

    std::string sanitizeExtension(const std::string &ext) {
        std::string e = ext;
        if (!e.empty() && e[0] == '.') {
            e = e.substr(1);
        }
        std::string out;
        for (char c: e) {
            if (c >= 'A' && c <= 'Z') {
                c = (char) (c - 'A' + 'a');
            }
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
                return "mkv";
            }
            out += c;
        }
        return out.empty() || out.size() > 5 ? "mkv" : out;
    }

    const char *folderOf(Kind kind) {
        return kind == Kind::Episode ? "episodes" : "movies";
    }

    std::string makeFileName(Kind kind, const std::string &contentId, const std::string &extension,
                             const std::string &profileId, const std::vector<std::string> &taken) {
        std::string base = std::string(kind == Kind::Episode ? "episode_" : "movie_") + sanitizeComponent(contentId);
        std::string ext = "." + sanitizeExtension(extension);
        auto free = [&taken](const std::string &name) {
            return std::find(taken.begin(), taken.end(), name) == taken.end();
        };
        std::string name = base + ext;
        if (free(name)) {
            return name;
        }
        std::string withProfile = base + "_" + sanitizeComponent(profileId);
        if (free(withProfile + ext)) {
            return withProfile + ext;
        }
        for (int i = 2; i < 10000; i++) {
            name = withProfile + "_" + std::to_string(i) + ext;
            if (free(name)) {
                return name;
            }
        }
        return withProfile + "_x" + ext;
    }

    std::string profileTag(const std::string &server, const std::string &username) {
        uint64_t h = 1469598103934665603ull;
        std::string text = server + "\n" + username;
        for (unsigned char c: text) {
            h ^= c;
            h *= 1099511628211ull;
        }
        char buf[17];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long) h);
        return buf;
    }

    // ------------------------------------------------------------------ manifest

    const char *stateKey(State s) {
        return STATE_KEYS[(int) s];
    }

    State stateFromKey(const std::string &key) {
        for (int i = 0; i < 7; i++) {
            if (key == STATE_KEYS[i]) {
                return (State) i;
            }
        }
        return State::Paused;
    }

    const char *problemKey(Problem p) {
        return PROBLEM_KEYS[(int) p];
    }

    Problem problemFromKey(const std::string &key) {
        for (int i = 1; i < 13; i++) {
            if (key == PROBLEM_KEYS[i]) {
                return (Problem) i;
            }
        }
        return Problem::None;
    }

    State recoveredState(State saved, bool autoResume) {
        switch (saved) {
            case State::Downloading:
            case State::Queued:
            case State::WaitingForNetwork:
                return autoResume ? State::Queued : State::Paused;
            default:
                return saved;
        }
    }

    std::string serializeManifest(const std::vector<Item> &items) {
        json::Value root = json::Value::makeObject();
        root.set("version", json::Value::makeInt(MANIFEST_VERSION));
        json::Value list = json::Value::makeArray();
        for (const Item &it: items) {
            if (it.state == State::Cancelled) {
                continue;
            }
            json::Value o = json::Value::makeObject();
            o.set("key", json::Value::makeString(it.key));
            o.set("type", json::Value::makeString(KIND_KEYS[(int) it.kind]));
            o.set("profileId", json::Value::makeString(it.profileId));
            o.set("profileName", json::Value::makeString(it.profileName));
            o.set("profileTag", json::Value::makeString(it.profileTag));
            o.set("contentId", json::Value::makeString(it.contentId));
            if (it.kind == Kind::Episode) {
                o.set("seriesId", json::Value::makeString(it.seriesId));
                o.set("seriesName", json::Value::makeString(it.seriesName));
                o.set("season", json::Value::makeInt(it.season));
                o.set("episode", json::Value::makeInt(it.episode));
            }
            o.set("title", json::Value::makeString(it.title));
            o.set("year", json::Value::makeInt(it.year));
            o.set("extension", json::Value::makeString(it.extension));
            o.set("poster", json::Value::makeString(it.poster));
            o.set("durationHint", json::Value::makeNumber(it.durationHint));
            o.set("file", json::Value::makeString(it.fileName));
            o.set("state", json::Value::makeString(stateKey(it.state)));
            if (it.problem != Problem::None) {
                o.set("problem", json::Value::makeString(problemKey(it.problem)));
                o.set("httpStatus", json::Value::makeInt(it.httpStatus));
                o.set("neededBytes", json::Value::makeInt(it.neededBytes));
                o.set("availableBytes", json::Value::makeInt(it.availableBytes));
            }
            o.set("expectedBytes", json::Value::makeInt(it.expectedBytes));
            o.set("downloadedBytes", json::Value::makeInt(it.downloadedBytes));
            o.set("durableBytes", json::Value::makeInt(it.durableBytes));
            o.set("etag", json::Value::makeString(it.etag));
            o.set("lastModified", json::Value::makeString(it.lastModified));
            o.set("createdAt", json::Value::makeInt(it.createdAt));
            o.set("completedAt", json::Value::makeInt(it.completedAt));
            o.set("order", json::Value::makeInt(it.order));
            list.push(std::move(o));
        }
        root.set("items", std::move(list));
        return json::write(root);
    }

    bool parseManifest(const std::string &text, std::vector<Item> &out, std::string *error) {
        out.clear();
        json::Value root;
        std::string err;
        if (!json::parse(text, root, &err) || !root.isObject()) {
            if (error) {
                *error = "downloads.json is corrupt: " + err;
            }
            return false;
        }
        int skipped = 0;
        std::vector<std::string> names;
        for (const json::Value &o: root["items"].items()) {
            Item it;
            std::string type = o["type"].asString();
            it.kind = type == "episode" ? Kind::Episode : Kind::Movie;
            it.profileId = o["profileId"].asString();
            it.contentId = o["contentId"].asString();
            it.fileName = o["file"].asString();
            // a file name is only accepted when it is exactly what makeFileName could produce: no traversal
            bool safeName = !it.fileName.empty() && it.fileName.find('/') == std::string::npos
                            && it.fileName.find('\\') == std::string::npos && it.fileName.find("..") == std::string::npos
                            && it.fileName.size() <= 128;
            if ((type != "movie" && type != "episode") || it.contentId.empty() || !safeName
                || std::find(names.begin(), names.end(), it.fileName) != names.end()) {
                skipped++;
                continue;
            }
            names.push_back(it.fileName);
            it.key = makeKey(it.profileId, it.kind, it.contentId);
            it.profileName = o["profileName"].asString();
            it.profileTag = o["profileTag"].asString();
            it.seriesId = o["seriesId"].asString();
            it.seriesName = o["seriesName"].asString();
            it.season = (int) o["season"].asInt();
            it.episode = (int) o["episode"].asInt();
            it.title = o["title"].asString();
            it.year = (int) o["year"].asInt();
            it.extension = sanitizeExtension(o["extension"].asString());
            it.poster = o["poster"].asString();
            it.durationHint = o["durationHint"].asDouble();
            it.state = stateFromKey(o["state"].asString());
            it.problem = problemFromKey(o["problem"].asString());
            it.httpStatus = (long) o["httpStatus"].asInt();
            it.neededBytes = o["neededBytes"].asInt();
            it.availableBytes = o["availableBytes"].asInt(-1);
            it.expectedBytes = o["expectedBytes"].asInt(-1);
            it.downloadedBytes = std::max<int64_t>(0, o["downloadedBytes"].asInt());
            it.durableBytes = std::max<int64_t>(0, o["durableBytes"].asInt());
            it.etag = o["etag"].asString();
            it.lastModified = o["lastModified"].asString();
            it.createdAt = o["createdAt"].asInt();
            it.completedAt = o["completedAt"].asInt();
            it.order = o["order"].asInt();
            if (it.state == State::Cancelled) {
                continue;
            }
            bool duplicate = false;
            for (const Item &e: out) {
                duplicate |= e.key == it.key;
            }
            if (duplicate) {
                skipped++;
                continue;
            }
            out.push_back(it);
        }
        if (skipped > 0 && error) {
            *error = std::to_string(skipped) + " broken download entr" + (skipped == 1 ? "y" : "ies") + " skipped";
        }
        return true;
    }

    // ------------------------------------------------------------------ HTTP

    ContentRange parseContentRange(const std::string &value) {
        ContentRange r;
        size_t pos = 0;
        skipSpaces(value, pos);
        if (value.compare(pos, 5, "bytes") != 0) {
            return r;
        }
        pos += 5;
        skipSpaces(value, pos);
        if (pos < value.size() && value[pos] == '*') {
            // "bytes */1000" (416 responses): only the total
            pos++;
            if (pos < value.size() && value[pos] == '/') {
                pos++;
                int64_t total;
                if (parseInt64(value, pos, total)) {
                    r.total = total;
                }
            }
            return r;
        }
        int64_t start, end;
        if (!parseInt64(value, pos, start) || pos >= value.size() || value[pos] != '-') {
            return r;
        }
        pos++;
        if (!parseInt64(value, pos, end) || pos >= value.size() || value[pos] != '/' || end < start) {
            return r;
        }
        pos++;
        if (pos < value.size() && value[pos] == '*') {
            r.total = -1;
        } else {
            int64_t total;
            if (!parseInt64(value, pos, total) || total <= end) {
                return r;
            }
            r.total = total;
        }
        r.valid = true;
        r.start = start;
        r.end = end;
        return r;
    }

    Problem problemForStatus(long status) {
        if (status == 401) {
            return Problem::HttpAuth;
        }
        if (status == 403) {
            return Problem::HttpForbidden;
        }
        if (status == 404 || status == 410) {
            return Problem::HttpNotFound;
        }
        return Problem::HttpServer;
    }

    Plan planResponse(int64_t offset, int64_t expectedTotal, const ResponseHead &h) {
        Plan p;
        if (h.status == 416) {
            int64_t total = h.range.total >= 0 ? h.range.total : expectedTotal;
            if (offset > 0 && total > 0 && offset == total) {
                p.action = Plan::Action::AlreadyComplete;
                p.total = total;
            } else if (offset > 0) {
                p.action = Plan::Action::RestartFromZero;
            } else {
                p.problem = Problem::HttpServer;
            }
            return p;
        }
        if (h.status < 200 || h.status >= 300 || h.status == 204) {
            p.problem = problemForStatus(h.status);
            return p;
        }
        if (h.status == 206) {
            if (!h.range.valid) {
                if (offset == 0) {
                    p.action = Plan::Action::Write;
                    p.total = h.contentLength;
                } else {
                    p.action = Plan::Action::RestartFromZero;   // a partial body we cannot place
                }
                return p;
            }
            int64_t total = h.range.total >= 0 ? h.range.total
                                               : h.contentLength >= 0 ? h.range.start + h.contentLength : -1;
            if (h.range.start != offset && h.range.start != 0) {
                p.action = Plan::Action::RestartFromZero;
                return p;
            }
            if (offset > 0 && h.range.start == offset && expectedTotal > 0 && total > 0 && total != expectedTotal) {
                p.action = Plan::Action::RestartFromZero;   // the file changed on the server
                return p;
            }
            p.action = Plan::Action::Write;
            p.writeOffset = h.range.start;
            p.total = total;
            return p;
        }
        // 200: the whole file from byte 0 (no Range sent, or the server ignored / refused it via If-Range)
        p.total = h.contentLength;
        if (offset > RESTART_CONFIRM_BYTES) {
            p.action = Plan::Action::AskRestart;   // never append blindly, never drop a large partial silently
            p.problem = Problem::RestartNeeded;
            return p;
        }
        p.action = Plan::Action::Write;
        p.writeOffset = 0;
        return p;
    }

    // ------------------------------------------------------------------ retry policy

    double backoffDelay(const RetryPolicy &policy, int attempt) {
        if (attempt < 1) {
            attempt = 1;
        }
        double d = policy.firstDelay;
        for (int i = 1; i < attempt && d < policy.maxDelay; i++) {
            d *= 2;
        }
        return std::min(d, policy.maxDelay);
    }

    bool isRetryable(Problem p) {
        return p == Problem::Network || p == Problem::HttpForbidden || p == Problem::HttpServer;
    }

    // ------------------------------------------------------------------ space

    int64_t safetyMargin(int64_t remaining) {
        const int64_t base = 512ll * 1024 * 1024;
        int64_t pct = remaining > 0 ? remaining / 50 : 0;
        return std::max(base, pct);
    }

    bool hasRoomFor(int64_t remaining, int64_t available) {
        if (available < 0) {
            return true;   // unknown: checked again while writing
        }
        return available >= std::max<int64_t>(0, remaining) + safetyMargin(remaining);
    }

    // ------------------------------------------------------------------ speed / ETA

    void SpeedMeter::reset(double now, int64_t bytes) {
        lastTime = now;
        lastBytes = bytes;
        speed = 0;
        primed = false;
    }

    void SpeedMeter::sample(double now, int64_t bytes) {
        double dt = now - lastTime;
        if (dt < 0.5) {
            return;
        }
        double inst = (double) (bytes - lastBytes) / dt;
        if (inst < 0) {
            inst = 0;
        }
        speed = primed ? 0.2 * inst + 0.8 * speed : inst;
        primed = true;
        lastTime = now;
        lastBytes = bytes;
    }

    double SpeedMeter::eta(int64_t remaining) const {
        if (remaining < 0 || speed < 1) {
            return -1;
        }
        return (double) remaining / speed;
    }

    // ------------------------------------------------------------------ display helpers

    std::string formatBytes(int64_t bytes) {
        if (bytes < 0) {
            bytes = 0;
        }
        const std::string &point = i18n::tr("format.decimal_point");
        auto fixed = [&point](double v, int decimals, const char *unit) {
            char buf[48];
            snprintf(buf, sizeof(buf), "%.*f", decimals, v);
            std::string s = buf;
            size_t dot = s.find('.');
            if (dot != std::string::npos) {
                s.replace(dot, 1, point);
            }
            return s + " " + unit;
        };
        if (bytes < 1024) {
            return std::to_string(bytes) + " B";
        }
        if (bytes < 1024 * 1024) {
            return std::to_string((bytes + 1023) / 1024) + " KB";
        }
        if (bytes < 1024ll * 1024 * 1024) {
            return fixed((double) bytes / (1024.0 * 1024.0), 1, "MB");
        }
        return fixed((double) bytes / (1024.0 * 1024.0 * 1024.0), 2, "GB");
    }

    int percent(int64_t done, int64_t total) {
        if (total <= 0) {
            return -1;
        }
        if (done >= total) {
            return 100;
        }
        return (int) ((double) done * 100.0 / (double) total);
    }
}
