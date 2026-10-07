#include <memory>

#include "xtream_service.h"
#include "../iptv/xtream.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/log.h"

using namespace iptv;

namespace {
    std::string host(const Profile &p) {
        size_t s = p.server.find("://");
        return s == std::string::npos ? p.server : p.server.substr(s + 3);
    }
}

CancelToken XtreamService::authenticate(const Profile &profile, AuthCallback callback) {
    auto out = std::make_shared<AuthOutcome>();
    Profile p = profile;
    return jobs.submit(JobPriority::High, "auth", [p, out](const CancelToken &token) {
        http::Request req;
        req.url = xtream::apiUrl(p);
        req.cancel = token.shared();
        req.totalTimeoutMs = 20000;
        req.maxBytes = 512 * 1024;
        http::Response resp = http::get(req);
        if (resp.error != http::Error::None) {
            out->result.status = AuthStatus::Network;
            out->message = http::describe(resp);
            LOG_W("xtream", "auth %s: %s (%s) after %.1fs", host(p).c_str(), out->message.c_str(),
                  resp.detail.c_str(), resp.seconds);
            return;
        }
        out->result = xtream::parseAuth(resp.status, resp.body, clockx::unixNow());
        out->message = xtream::authStatusText(out->result.status);
        bool httpsServer = p.server.compare(0, 8, "https://") == 0;
        out->httpsWarning = out->result.status == AuthStatus::Ok
                            && (httpsServer || out->result.account.serverProtocol == "https" || resp.redirectedToHttps);
        LOG_I("xtream", "auth %s: HTTP %ld in %.2fs, %s (%s), max %d / active %d connections%s", host(p).c_str(),
              resp.status, resp.seconds, out->message.c_str(), out->result.detail.c_str(),
              out->result.account.maxConnections, out->result.account.activeConnections,
              out->httpsWarning ? ", streams need HTTPS" : "");
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken XtreamService::loadCategories(const Profile &profile, ContentType type, CategoriesCallback callback) {
    auto out = std::make_shared<CategoriesOutcome>();
    Profile p = profile;
    const char *action = type == ContentType::Live ? "get_live_categories"
                         : type == ContentType::Movie ? "get_vod_categories" : "get_series_categories";
    return jobs.submit(JobPriority::High, action, [p, out, action, type](const CancelToken &token) {
        http::Request req;
        req.url = xtream::apiUrl(p, action);
        req.cancel = token.shared();
        req.maxBytes = 8u * 1024 * 1024;
        http::Response resp = http::get(req);
        if (!resp.ok()) {
            out->message = http::describe(resp);
            LOG_W("xtream", "%s %s: %s (%s)", action, host(p).c_str(), out->message.c_str(), resp.detail.c_str());
            return;
        }
        std::string err;
        out->ok = xtream::parseCategories(resp.body, out->categories, err);
        if (!out->ok) {
            out->message = std::string("Could not read the ") + contentTypeName(type) + " categories";
            LOG_W("xtream", "%s: parse failed: %s", action, err.c_str());
            return;
        }
        LOG_I("xtream", "%s: %d categories (%zu bytes, %.2fs)", action, (int) out->categories.size(),
              resp.body.size(), resp.seconds);
    }, [out, callback]() {
        callback(*out);
    });
}
