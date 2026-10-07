#include <memory>

#include "xtream_service.h"
#include "../iptv/xtream.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"

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

CancelToken XtreamService::loadLive(const Profile &profile, const std::string &dataDir, LiveCallback callback) {
    auto out = std::make_shared<LiveOutcome>();
    Profile p = profile;
    return jobs.submit(JobPriority::High, "live", [p, out, dataDir](const CancelToken &token) {
        CatalogCache cache(dataDir);
        std::string catBody, streamBody, err;

        http::Request req;
        req.cancel = token.shared();
        req.maxBytes = 8u * 1024 * 1024;
        req.url = xtream::apiUrl(p, "get_live_categories");
        http::Response cats = http::get(req);
        http::Response streams;
        if (cats.ok()) {
            req.url = xtream::apiUrl(p, "get_live_streams");
            req.maxBytes = 96u * 1024 * 1024;
            req.totalTimeoutMs = 120000;
            streams = http::get(req);
        }
        if (token.canceled()) {
            return;
        }
        if (cats.ok() && streams.ok() && xtream::parseCategories(cats.body, out->categories, err)
            && xtream::parseLiveStreams(streams.body, out->channels, err)) {
            out->ok = true;
            catBody = std::move(cats.body);
            streamBody = std::move(streams.body);
            int64_t now = clockx::unixNow();
            std::string cacheErr;
            if (!cache.save(p.id, "live_categories", catBody, now, &cacheErr)
                || !cache.save(p.id, "live_streams", streamBody, now, &cacheErr)) {
                LOG_W("xtream", "live cache not saved: %s", cacheErr.c_str());
            }
            LOG_I("xtream", "live: %d categories, %d channels (%zu KiB, %.1fs)", (int) out->categories.size(),
                  (int) out->channels.size(), (streamBody.size() + 1023) / 1024, cats.seconds + streams.seconds);
        } else {
            const http::Response &bad = !cats.ok() ? cats : streams;
            std::string why = bad.ok() ? "Could not read the channel list" : http::describe(bad);
            LOG_W("xtream", "live list from network failed: %s (%s%s)", why.c_str(), bad.detail.c_str(),
                  err.empty() ? "" : (", " + err).c_str());
            int64_t savedCats = 0, savedStreams = 0;
            out->categories.clear();
            out->channels.clear();
            if (cache.load(p.id, "live_categories", catBody, savedCats)
                && cache.load(p.id, "live_streams", streamBody, savedStreams)
                && xtream::parseCategories(catBody, out->categories, err)
                && xtream::parseLiveStreams(streamBody, out->channels, err)) {
                out->ok = true;
                out->fromCache = true;
                out->savedAt = savedStreams;
                out->message = why + " - showing the saved list";
                LOG_I("xtream", "live: using cache from %lld (%d channels)", (long long) savedStreams,
                      (int) out->channels.size());
            } else {
                out->message = why;
            }
        }
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
