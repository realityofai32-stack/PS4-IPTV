#include "m3u_service.h"
#include "../i18n/i18n.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/fs.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"

using namespace iptv;

namespace {
    // what the log may say about a playlist location: host + file name, never credentials / tokens
    std::string where(const Profile &p) {
        std::string shown = m3u::displayUrl(p.playlistUrl);
        return shown.empty() ? "?" : shown;
    }
}

std::string M3uService::errorText(m3u::Error error) {
    switch (error) {
        case m3u::Error::Empty:
            return i18n::tr("m3u.error_empty");
        case m3u::Error::HlsMedia:
            return i18n::tr("m3u.error_hls");
        case m3u::Error::Utf16:
            return i18n::tr("m3u.error_encoding");
        default:
            return i18n::tr("m3u.error_not_playlist");
    }
}

bool M3uService::validLocation(const std::string &location) {
    return m3u::isHttpUrl(location) || !m3u::localPlaylistPath(location).empty();
}

bool M3uService::saveBody(const std::string &dataDir, const std::string &profileId, const std::string &body,
                          int channels, int64_t now) {
    std::string err;
    if (!CatalogCache(dataDir).save(profileId, CACHE_NAME, body, now, channels, &err)) {
        LOG_W("m3u", "playlist not saved: %s", err.c_str());
        return false;
    }
    return true;
}

CancelToken M3uService::load(const Profile &profile, const std::string &dataDir, Source source, bool save,
                             Callback callback, bool keepBody) {
    auto out = std::make_shared<Outcome>();
    Profile p = profile;
    bool fromCache = source == Source::Cache;
    return jobs.submit(fromCache ? JobPriority::High : JobPriority::Normal, fromCache ? "m3u (cache)" : "m3u",
                       [=](const CancelToken &token) {
        auto body = std::make_shared<std::string>();
        CatalogCache cache(dataDir);
        CatalogCache::Meta meta;
        double fetchSeconds = 0;
        if (fromCache) {
            if (!cache.load(p.id, CACHE_NAME, *body, meta)) {
                out->message = i18n::tr("m3u.no_saved_copy");
                return;
            }
        } else {
            std::string local = m3u::localPlaylistPath(p.playlistUrl);
            double t0 = clockx::monotonic();
            if (!local.empty()) {
                std::string err;
                if (!fs::readFile(local, *body, MAX_BYTES, &err)) {
                    out->networkFailure = true;
                    out->message = i18n::tr("m3u.local_unreadable");
                    LOG_W("m3u", "local playlist %s: %s", where(p).c_str(), err.c_str());
                    return;
                }
            } else if (m3u::isHttpUrl(p.playlistUrl)) {
                http::Request req;
                req.url = p.playlistUrl;
                req.cancel = token.shared();
                req.connectTimeoutMs = 10000;
                req.totalTimeoutMs = 180000;   // tens of thousands of entries on a slow server
                req.stallTimeoutS = 30;
                req.maxBytes = MAX_BYTES;
                req.userAgent = p.userAgent;
                http::Response resp = http::get(req);
                if (token.canceled()) {
                    return;
                }
                if (!resp.ok()) {
                    out->networkFailure = true;
                    out->message = http::describe(resp);
                    LOG_W("m3u", "download %s: %s (%s) after %.1fs", where(p).c_str(), out->message.c_str(),
                          resp.detail.c_str(), resp.seconds);
                    return;
                }
                *body = std::move(resp.body);
            } else {
                out->message = i18n::tr("m3u.error_location");
                return;
            }
            fetchSeconds = clockx::monotonic() - t0;
            meta.savedAt = clockx::unixNow();
        }
        if (token.canceled()) {
            return;
        }

        auto catalog = std::make_shared<LiveCatalog>();
        m3u::Error e = m3u::build(p.id, *body, *catalog, out->info);
        const m3u::Stats &st = out->info.stats;
        if (e != m3u::Error::None) {
            out->message = errorText(e);
            LOG_W("m3u", "%s%s: not a usable channel playlist (error %d, %zu bytes, %d lines, %d entries skipped)",
                  where(p).c_str(), fromCache ? " (saved copy)" : "", (int) e, st.bytes, st.lines, st.skipped());
            if (fromCache) {
                cache.remove(p.id, CACHE_NAME);   // never read a broken saved copy again
            }
            return;
        }
        if (fromCache && meta.items >= 0 && meta.items != (int) catalog->channels().size()) {
            LOG_W("m3u", "saved playlist: %d channels parsed, %d saved: discarded", (int) catalog->channels().size(),
                  meta.items);
            cache.remove(p.id, CACHE_NAME);
            out->message = i18n::tr("m3u.no_saved_copy");
            return;
        }
        if (!fromCache && save) {
            std::string err;
            double t0 = clockx::monotonic();
            if (!cache.save(p.id, CACHE_NAME, *body, meta.savedAt, (int) catalog->channels().size(), &err)) {
                LOG_W("m3u", "playlist not saved: %s", err.c_str());
            } else {
                LOG_I("m3u", "playlist saved in %.0f ms", (clockx::monotonic() - t0) * 1000);
            }
        }
        out->info.fromCache = fromCache;
        out->info.savedAt = meta.savedAt;
        catalog->fromCache = fromCache;
        catalog->loadedAt = meta.savedAt;
        LOG_I("m3u", "%s%s: %d channels in %d groups (%zu KiB%s), %d with logo, %d skipped (missing URL %d, "
                     "invalid URL %d, over limit %d), %d malformed EXTINF, %d unknown directives, %d HLS, %d HTTPS, "
                     "%d other protocols, %d own User-Agent, %d duplicate ids; parse %.0f ms, index %.0f ms",
              where(p).c_str(), fromCache ? " (saved copy)" : "", st.channels, st.groups, (body->size() + 1023) / 1024,
              fromCache ? "" : diag::format(", %.1fs download", fetchSeconds).c_str(), st.withLogo, st.skipped(),
              st.missingUrl, st.invalidUrl, st.overLimit, st.malformedExtinf, st.unknownDirectives, st.hls, st.https,
              st.otherProtocols, st.userAgents, st.duplicateIds, out->info.parseMs, out->info.indexMs);
        out->catalog = catalog;
        out->ok = true;
        if (keepBody) {
            out->body = body;
        }
    }, [out, callback]() {
        callback(*out);
    });
}
