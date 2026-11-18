#include <unordered_set>

#include "mediabrowser_service.h"
#include "../i18n/i18n.h"
#include "../core/url.h"
#include "../iptv/xtream.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../platform/redact.h"
#include "../storage/catalog_cache.h"

using namespace iptv;

namespace {
    const size_t MAX_LIST_BYTES = 32u << 20;   // one page of 500 items is far below this
    const size_t MAX_ITEM_BYTES = 4u << 20;
    const char *const PUBLIC_INFO = "/System/Info/Public";

    struct Call {
        const std::string &base;
        const std::vector<std::string> &headers;
        const CancelToken &token;
    };

    http::Response get(const Call &c, const std::string &path, size_t maxBytes, long timeoutMs = 30000) {
        http::Request req;
        req.url = c.base + path;
        req.cancel = c.token.shared();
        req.headers = c.headers;
        req.connectTimeoutMs = 10000;
        req.totalTimeoutMs = timeoutMs;
        req.maxBytes = maxBytes;
        return http::get(req);
    }

    // user-facing text of a failed request
    std::string failure(const http::Response &r) {
        return r.error == http::Error::None && (r.status == 401 || r.status == 403) ? i18n::tr("media.signed_out")
                                                                                     : http::describe(r);
    }

    template<typename Catalog, typename Item>
    std::shared_ptr<Catalog> buildCatalog(std::vector<Category> categories, std::vector<Item> items, int64_t savedAt,
                                          bool fromCache) {
        auto catalog = std::make_shared<Catalog>();
        int parsed = (int) items.size();
        double t0 = clockx::monotonic();
        catalog->assign(std::move(categories), std::move(items));
        double t1 = clockx::monotonic();
        catalog->buildSearch();
        double t2 = clockx::monotonic();
        catalog->buildSortKeys();
        double t3 = clockx::monotonic();
        catalog->savedAt = savedAt;
        catalog->fromCache = fromCache;
        CatalogDiagnostics &d = catalog->diagnostics();
        d.parse.raw = parsed;
        d.parse.parsed = parsed;
        d.cached = parsed;
        d.fromCache = fromCache;
        d.parseMs = (t1 - t0) * 1000;
        d.indexMs = (t2 - t1) * 1000;
        d.sortMs = (t3 - t2) * 1000;
        return catalog;
    }

    // the libraries of one kind (movies / tvshows; mixed libraries for both) and their items, page by page
    template<typename Item, typename Parse>
    bool crawl(const Call &c, const std::string &userId, bool series, std::vector<Category> &categories,
               std::vector<Item> &items, std::string &error, Parse parse) {
        http::Response r = get(c, "/Users/" + url::encode(userId) + "/Views", MAX_ITEM_BYTES);
        std::vector<mediabrowser::Library> libraries;
        if (!r.ok() || !mediabrowser::parseViews(r.body, libraries)) {
            error = r.ok() ? i18n::tr("media.unreadable") : failure(r);
            return false;
        }
        int pages = 0;
        for (const auto &l: libraries) {
            bool wanted = l.type.empty() || l.type == "mixed" || l.type == (series ? "tvshows" : "movies");
            if (!wanted || (int) items.size() >= mediabrowser::MAX_ITEMS) {
                continue;
            }
            size_t before = items.size();
            for (int start = 0; (int) items.size() < mediabrowser::MAX_ITEMS;) {
                if (c.token.canceled()) {
                    return false;
                }
                int limit = std::min(mediabrowser::PAGE, mediabrowser::MAX_ITEMS - (int) items.size());
                http::Response p = get(c, mediabrowser::itemsPath(userId, l.id, series, start, limit), MAX_LIST_BYTES, 60000);
                mediabrowser::Page page;
                if (!p.ok() || !parse(p.body, l.id, items, page)) {
                    error = p.ok() ? i18n::tr("media.unreadable") : failure(p);
                    return false;
                }
                pages++;
                start += page.items;
                if (page.items == 0 || start >= page.total) {
                    break;
                }
            }
            if (items.size() > before) {
                categories.push_back({l.id, l.name, ""});   // only libraries that have something of this kind
            }
        }
        LOG_I("mediaserver", "%s: %zu libraries, %zu items in %d pages", series ? "series" : "movies", libraries.size(),
              items.size(), pages);
        return true;
    }

    template<typename Item, typename Id>
    void dropRepeats(std::vector<Item> &items, Id id) {
        std::unordered_set<std::string> seen;
        std::vector<Item> kept;
        kept.reserve(items.size());
        for (auto &i: items) {
            if (seen.insert(id(i)).second) {
                kept.push_back(std::move(i));   // an item in two libraries: listed under the first
            }
        }
        items = std::move(kept);
    }
}

MediaBrowserService::MediaBrowserService(JobSystem &j, std::string version) : jobs(j), appVersion(std::move(version)) {}

std::shared_ptr<const MediaBrowserService::Session> MediaBrowserService::sessionFor(const Profile &p) const {
    auto it = sessions.find(p.id);
    if (it == sessions.end()) {
        return nullptr;
    }
    std::string err;
    // the source was edited since its sign-in (another server / user): that sign-in does not count
    bool same = it->second->entered == mediabrowser::serverBase(p.server, err) && it->second->user == p.username;
    return same ? it->second : nullptr;
}

bool MediaBrowserService::signedIn(const Profile &p) const {
    return sessionFor(p) != nullptr;
}

std::string MediaBrowserService::serverName(const Profile &p) const {
    auto s = sessionFor(p);
    return s ? s->product : "";
}

CancelToken MediaBrowserService::authenticate(const Profile &profile, sources::AuthCallback callback) {
    auto out = std::make_shared<sources::AuthOutcome>();
    auto made = std::make_shared<Session>();
    Profile p = profile;
    std::string version = appVersion;
    return jobs.submit(JobPriority::High, "mediaserver sign-in", [out, made, p, version](const CancelToken &token) {
        std::string err;
        std::string base = mediabrowser::serverBase(p.server, err);
        if (base.empty()) {
            out->result.status = AuthStatus::Malformed;
            out->message = err;
            return;
        }
        std::string device = mediabrowser::deviceId(p.id);
        std::vector<std::string> plain = mediabrowser::headers(device, version, "");
        // what answers there (and where, after a redirect to https)
        http::Request info;
        info.url = base + PUBLIC_INFO;
        info.cancel = token.shared();
        info.headers = plain;
        info.connectTimeoutMs = 10000;
        info.totalTimeoutMs = 20000;
        info.maxBytes = 256 * 1024;
        http::Response r = http::get(info);
        if (token.canceled()) {
            return;
        }
        mediabrowser::ServerInfo si;
        if (r.error != http::Error::None) {
            out->result.status = AuthStatus::Network;
            out->message = http::describe(r);
            out->result.detail = "public info: " + r.detail;
            return;
        }
        if (!r.ok() || !mediabrowser::parsePublicInfo(r.body, si)) {
            out->result.status = AuthStatus::Malformed;
            out->message = i18n::tr("media.not_server");
            out->result.detail = "public info: HTTP " + std::to_string(r.status);
            return;
        }
        if (r.finalUrl.size() > std::char_traits<char>::length(PUBLIC_INFO)
            && r.finalUrl.compare(r.finalUrl.size() - std::char_traits<char>::length(PUBLIC_INFO),
                                  std::char_traits<char>::length(PUBLIC_INFO), PUBLIC_INFO) == 0) {
            base = r.finalUrl.substr(0, r.finalUrl.size() - std::char_traits<char>::length(PUBLIC_INFO));
        }
        http::Request login;
        login.url = base + "/Users/AuthenticateByName";
        login.cancel = token.shared();
        login.headers = plain;
        login.headers.push_back("Content-Type: application/json");
        login.post = true;
        login.body = mediabrowser::loginBody(p.username, p.password);
        login.connectTimeoutMs = 10000;
        login.totalTimeoutMs = 30000;
        login.maxBytes = 1024 * 1024;
        login.retries = 0;
        http::Response a = http::get(login);
        if (token.canceled()) {
            return;
        }
        mediabrowser::Login l;
        if (a.error != http::Error::None) {
            out->result.status = AuthStatus::Network;
            out->message = http::describe(a);
            out->result.detail = "sign-in: " + a.detail;
            return;
        }
        if (a.status == 401 || a.status == 403) {
            out->result.status = AuthStatus::InvalidCredentials;
            out->message = i18n::tr("media.invalid");
            return;
        }
        if (!a.ok() || !mediabrowser::parseLogin(a.body, l)) {
            out->result.status = a.status >= 500 ? AuthStatus::ServerError : AuthStatus::Malformed;
            out->message = i18n::tr(a.status >= 500 ? "media.server_error" : "media.unreadable");
            out->result.detail = "sign-in: HTTP " + std::to_string(a.status);
            return;
        }
        redact::addSecret(l.token);   // never in a log line, also not inside a stream address
        std::string ignored;
        made->entered = mediabrowser::serverBase(p.server, ignored);
        made->user = p.username;
        made->base = base;
        made->token = l.token;
        made->userId = l.userId;
        made->product = si.product + (si.version.empty() ? "" : " " + si.version);
        made->headers = mediabrowser::headers(device, version, l.token);
        out->result.status = AuthStatus::Ok;
        out->result.account.status = "Active";   // i18n-exempt: technical value (an Xtream field)
        out->result.account.message = made->product;
        out->message = xtream::authStatusText(AuthStatus::Ok);
        LOG_I("mediaserver", "signed in to %s (%s)", made->product.c_str(),
              r.redirectedToHttps ? "moved to https" : base.compare(0, 8, "https://") == 0 ? "https" : "http");
    }, [this, out, made, p, callback]() {
        if (out->result.status == AuthStatus::Ok) {
            sessions[p.id] = made;
        } else {
            sessions.erase(p.id);
            LOG_W("mediaserver", "sign-in failed: %s (%s)", out->message.c_str(), out->result.detail.c_str());
        }
        callback(*out);
    });
}

CancelToken MediaBrowserService::loadMovies(const Profile &profile, const std::string &dataDir, sources::Source source,
                                            std::function<void(sources::MoviesOutcome &)> callback) {
    auto out = std::make_shared<sources::MoviesOutcome>();
    std::shared_ptr<const Session> s = sessionFor(profile);
    std::string profileId = profile.id;
    bool fromCache = source == sources::Source::Cache;
    return jobs.submit(fromCache ? JobPriority::High : JobPriority::Normal, fromCache ? "mediaserver movies (cache)"
                                                                                     : "mediaserver movies",
                       [=](const CancelToken &token) {
        CatalogCache cache(dataDir);
        std::vector<Category> categories;
        std::vector<Movie> movies;
        if (fromCache) {
            std::string body;
            CatalogCache::Meta meta;
            if (!cache.load(profileId, CACHE_MOVIES, body, meta) || !mediabrowser::decodeMovies(body, categories, movies)) {
                out->message = i18n::tr("catalog.no_saved_copy");
                return;
            }
            out->savedAt = meta.savedAt;
        } else {
            if (!s) {
                out->message = i18n::tr("media.signed_out");
                return;
            }
            Call c{s->base, s->headers, token};
            std::string err;
            bool ok = crawl<Movie>(c, s->userId, false, categories, movies, err,
                                   [&s](const std::string &body, const std::string &lib, std::vector<Movie> &items,
                                        mediabrowser::Page &page) {
                                       return mediabrowser::parseMovies(body, s->base, lib, items, page);
                                   });
            if (token.canceled()) {
                return;
            }
            if (!ok) {
                out->message = err;
                LOG_W("mediaserver", "movies failed: %s", err.c_str());
                return;
            }
            dropRepeats(movies, [](const Movie &m) { return m.streamId; });
            out->savedAt = clockx::unixNow();
            std::string cacheErr;
            if (!movies.empty() && !cache.save(profileId, CACHE_MOVIES, mediabrowser::encodeMovies(categories, movies),
                                               out->savedAt, -1, &cacheErr)) {
                LOG_W("mediaserver", "movies not saved: %s", cacheErr.c_str());
            }
        }
        if (movies.empty()) {
            out->message = i18n::tr("catalog.movies_empty");
            return;
        }
        out->catalog = buildCatalog<MovieCatalog>(std::move(categories), std::move(movies), out->savedAt, fromCache);
        out->ok = true;
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken MediaBrowserService::loadSeriesList(const Profile &profile, const std::string &dataDir,
                                                sources::Source source,
                                                std::function<void(sources::SeriesListOutcome &)> callback) {
    auto out = std::make_shared<sources::SeriesListOutcome>();
    std::shared_ptr<const Session> s = sessionFor(profile);
    std::string profileId = profile.id;
    bool fromCache = source == sources::Source::Cache;
    return jobs.submit(fromCache ? JobPriority::High : JobPriority::Normal, fromCache ? "mediaserver series (cache)"
                                                                                     : "mediaserver series",
                       [=](const CancelToken &token) {
        CatalogCache cache(dataDir);
        std::vector<Category> categories;
        std::vector<Series> series;
        if (fromCache) {
            std::string body;
            CatalogCache::Meta meta;
            if (!cache.load(profileId, CACHE_SERIES, body, meta) || !mediabrowser::decodeSeries(body, categories, series)) {
                out->message = i18n::tr("catalog.no_saved_copy");
                return;
            }
            out->savedAt = meta.savedAt;
        } else {
            if (!s) {
                out->message = i18n::tr("media.signed_out");
                return;
            }
            Call c{s->base, s->headers, token};
            std::string err;
            bool ok = crawl<Series>(c, s->userId, true, categories, series, err,
                                    [&s](const std::string &body, const std::string &lib, std::vector<Series> &items,
                                         mediabrowser::Page &page) {
                                        return mediabrowser::parseSeries(body, s->base, lib, items, page);
                                    });
            if (token.canceled()) {
                return;
            }
            if (!ok) {
                out->message = err;
                LOG_W("mediaserver", "series failed: %s", err.c_str());
                return;
            }
            dropRepeats(series, [](const Series &x) { return x.seriesId; });
            out->savedAt = clockx::unixNow();
            std::string cacheErr;
            if (!series.empty() && !cache.save(profileId, CACHE_SERIES, mediabrowser::encodeSeries(categories, series),
                                               out->savedAt, -1, &cacheErr)) {
                LOG_W("mediaserver", "series not saved: %s", cacheErr.c_str());
            }
        }
        if (series.empty()) {
            out->message = i18n::tr("catalog.series_empty");
            return;
        }
        out->catalog = buildCatalog<SeriesCatalog>(std::move(categories), std::move(series), out->savedAt, fromCache);
        out->ok = true;
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken MediaBrowserService::loadMovieInfo(const Profile &profile, const std::string &id,
                                               std::function<void(sources::InfoOutcome<MovieInfo> &)> callback) {
    auto out = std::make_shared<sources::InfoOutcome<MovieInfo>>();
    std::shared_ptr<const Session> s = sessionFor(profile);
    return jobs.submit(JobPriority::High, "mediaserver movie", [out, s, id](const CancelToken &token) {
        if (!s) {
            out->message = i18n::tr("media.signed_out");
            return;
        }
        Call c{s->base, s->headers, token};
        http::Response r = get(c, "/Users/" + url::encode(s->userId) + "/Items/" + url::encode(id), MAX_ITEM_BYTES);
        if (token.canceled()) {
            return;
        }
        if (!r.ok() || !mediabrowser::parseMovieInfo(r.body, s->base, out->info)) {
            out->message = r.ok() ? i18n::tr("media.unreadable") : failure(r);
            return;
        }
        out->ok = true;
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken MediaBrowserService::loadSeriesInfo(const Profile &profile, const std::string &id,
                                                std::function<void(sources::InfoOutcome<SeriesInfo> &)> callback) {
    auto out = std::make_shared<sources::InfoOutcome<SeriesInfo>>();
    std::shared_ptr<const Session> s = sessionFor(profile);
    return jobs.submit(JobPriority::High, "mediaserver series info", [out, s, id](const CancelToken &token) {
        if (!s) {
            out->message = i18n::tr("media.signed_out");
            return;
        }
        Call c{s->base, s->headers, token};
        http::Response d = get(c, "/Users/" + url::encode(s->userId) + "/Items/" + url::encode(id), MAX_ITEM_BYTES);
        if (token.canceled()) {
            return;
        }
        out->info.seriesId = id;
        out->info.series.seriesId = id;
        if (!d.ok() || !mediabrowser::parseSeriesDetail(d.body, s->base, out->info.series)) {
            out->message = d.ok() ? i18n::tr("media.unreadable") : failure(d);
            return;
        }
        http::Response e = get(c, "/Shows/" + url::encode(id) + "/Episodes?UserId=" + url::encode(s->userId)
                                  + "&Fields=Overview", MAX_LIST_BYTES, 60000);
        if (token.canceled()) {
            return;
        }
        if (!e.ok() || !mediabrowser::parseEpisodes(e.body, s->base, out->info)) {
            out->message = e.ok() ? i18n::tr("media.unreadable") : failure(e);
            return;
        }
        out->ok = true;
    }, [out, callback]() {
        callback(*out);
    });
}

std::string MediaBrowserService::streamUrl(const Profile &profile, const std::string &itemId) const {
    auto s = sessionFor(profile);
    return s ? mediabrowser::streamUrl(s->base, s->token, itemId) : "";
}

void MediaBrowserService::report(const Profile &profile, mediabrowser::Report what, const std::string &itemId,
                                 const std::string &playSessionId, double positionSeconds, bool paused) {
    auto s = sessionFor(profile);
    if (!s || itemId.empty()) {
        return;
    }
    std::string body = mediabrowser::reportBody(itemId, playSessionId, positionSeconds, paused);
    const char *path = mediabrowser::reportPath(what);
    jobs.submit(JobPriority::Normal, "mediaserver report", [s, body, path](const CancelToken &token) {
        http::Request req;
        req.url = s->base + path;
        req.cancel = token.shared();
        req.headers = s->headers;
        req.headers.push_back("Content-Type: application/json");
        req.post = true;
        req.body = body;
        req.connectTimeoutMs = 8000;
        req.totalTimeoutMs = 15000;
        req.maxBytes = 64 * 1024;
        req.retries = 0;
        http::Response r = http::get(req);
        if (!r.ok() && !token.canceled()) {
            LOG_W("mediaserver", "progress report %s: %s (HTTP %ld)", path, http::describe(r).c_str(), r.status);
        }
    }, []() {});
}
