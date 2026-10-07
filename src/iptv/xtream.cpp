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
