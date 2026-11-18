#include <unordered_set>

#include "stremio_service.h"
#include "../i18n/i18n.h"
#include "../iptv/m3u.h"
#include "../iptv/mediabrowser.h"
#include "../iptv/xtream.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../storage/catalog_cache.h"

using namespace iptv;

namespace {
    const size_t MAX_BYTES = 16u << 20;

    http::Response get(const std::string &url, const CancelToken &token, long timeoutMs) {
        http::Request req;
        req.url = url;
        req.cancel = token.shared();
        req.connectTimeoutMs = 10000;
        req.totalTimeoutMs = timeoutMs;
        req.maxBytes = MAX_BYTES;
        return http::get(req);
    }

    template<typename Catalog, typename Item>
    std::shared_ptr<Catalog> buildCatalog(std::vector<Category> categories, std::vector<Item> items, int64_t savedAt,
                                          bool fromCache) {
        auto catalog = std::make_shared<Catalog>();
        int parsed = (int) items.size();
        catalog->assign(std::move(categories), std::move(items));
        catalog->buildSearch();
        catalog->buildSortKeys();
        catalog->savedAt = savedAt;
        catalog->fromCache = fromCache;
        CatalogDiagnostics &d = catalog->diagnostics();
        d.parse.raw = parsed;
        d.parse.parsed = parsed;
        d.cached = parsed;
        d.fromCache = fromCache;
        return catalog;
    }

    // every catalog of one type, page by page (skip), the first catalog of a title keeping it; a catalog that fails
    // is left out when others answered
    template<typename Item, typename Parse, typename Id>
    bool crawl(const std::string &base, const stremio::Manifest &m, const std::string &type, const CancelToken &token,
               std::vector<Category> &categories, std::vector<Item> &items, std::string &error, Parse parse, Id id) {
        std::unordered_set<std::string> seen;
        int pages = 0;
        int failed = 0;
        for (const auto &c: m.catalogs) {
            if (c.type != type || (int) items.size() >= stremio::MAX_ITEMS) {
                continue;
            }
            std::string cat = stremio::categoryId(c);
            size_t before = items.size();
            for (int page = 0, skip = 0; page < (c.pages ? stremio::MAX_PAGES : 1); page++) {
                if (token.canceled()) {
                    return false;
                }
                http::Response r = get(stremio::catalogUrl(base, c, skip), token, 30000);
                std::vector<Item> got;
                int count = 0;
                if (!r.ok() || !parse(r.body, cat, got, count)) {
                    if (page == 0) {
                        failed++;
                        error = r.ok() ? i18n::tr("stremio.unreadable") : http::describe(r);
                    }
                    break;
                }
                pages++;
                int added = 0;
                for (auto &i: got) {
                    if ((int) items.size() < stremio::MAX_ITEMS && seen.insert(id(i)).second) {
                        items.push_back(std::move(i));
                        added++;
                    }
                }
                if (count == 0 || added == 0) {
                    break;   // the end, or an add-on that ignores "skip" (the same page again)
                }
                skip += count;
            }
            if (items.size() > before) {
                categories.push_back({cat, c.name, ""});
            }
        }
        LOG_I("stremio", "%s: %d catalogs, %zu titles in %d pages (%d catalogs failed)", type.c_str(),
              (int) categories.size(), items.size(), pages, failed);
        return !items.empty() || failed == 0;
    }
}

std::shared_ptr<const StremioService::Addon> StremioService::addonFor(const Profile &p) const {
    auto it = addons.find(p.id);
    std::string err;
    return it != addons.end() && it->second->manifestUrl == stremio::manifestUrl(p.playlistUrl, err) ? it->second
                                                                                                    : nullptr;
}

CancelToken StremioService::authenticate(const Profile &profile, sources::AuthCallback callback) {
    auto out = std::make_shared<sources::AuthOutcome>();
    auto made = std::make_shared<Addon>();
    Profile p = profile;
    return jobs.submit(JobPriority::High, "stremio manifest", [out, made, p](const CancelToken &token) {
        std::string err;
        std::string manifest = stremio::manifestUrl(p.playlistUrl, err);
        if (manifest.empty()) {
            out->result.status = AuthStatus::Malformed;
            out->message = err;
            return;
        }
        http::Response r = get(manifest, token, 20000);
        if (token.canceled()) {
            return;
        }
        if (r.error != http::Error::None) {
            out->result.status = AuthStatus::Network;
            out->message = http::describe(r);
            out->result.detail = r.detail;
            return;
        }
        stremio::Manifest m;
        if (!r.ok() || !stremio::parseManifest(r.body, m)) {
            out->result.status = AuthStatus::Malformed;
            out->message = i18n::tr("stremio.not_addon");
            out->result.detail = "manifest: HTTP " + std::to_string(r.status);
            return;
        }
        if (m.configurationRequired) {
            out->result.status = AuthStatus::Refused;
            out->message = i18n::tr("stremio.needs_configuration");
            return;
        }
        if (m.catalogs.empty()) {
            out->result.status = AuthStatus::Refused;
            out->message = i18n::tr("stremio.nothing_to_list");
            return;
        }
        if (!m.stream) {
            out->result.status = AuthStatus::Refused;
            out->message = i18n::tr("stremio.no_streams");
            return;
        }
        made->manifestUrl = manifest;
        made->base = stremio::baseOf(manifest);
        made->manifest = m;
        out->result.status = AuthStatus::Ok;
        out->result.account.status = "Active";   // i18n-exempt: technical value (an Xtream field)
        out->result.account.message = m.name + (m.version.empty() ? "" : " " + m.version) + "  \xC2\xB7  "
                                      + i18n::tr("stremio.catalogs", {std::to_string(m.catalogs.size())});
        out->message = xtream::authStatusText(AuthStatus::Ok);
        LOG_I("stremio", "add-on at %s: %zu catalogs, details %s, streams", m3u::logUrl(manifest).c_str(),
              m.catalogs.size(), m.meta ? "yes" : "no");
    }, [this, out, made, p, callback]() {
        if (out->result.status == AuthStatus::Ok) {
            addons[p.id] = made;
        } else {
            addons.erase(p.id);
            LOG_W("stremio", "add-on not usable: %s (%s)", out->message.c_str(), out->result.detail.c_str());
        }
        callback(*out);
    });
}

CancelToken StremioService::loadMovies(const Profile &profile, const std::string &dataDir, sources::Source source,
                                       std::function<void(sources::MoviesOutcome &)> callback) {
    auto out = std::make_shared<sources::MoviesOutcome>();
    std::shared_ptr<const Addon> a = addonFor(profile);
    std::string profileId = profile.id;
    bool fromCache = source == sources::Source::Cache;
    return jobs.submit(fromCache ? JobPriority::High : JobPriority::Normal, fromCache ? "stremio movies (cache)"
                                                                                     : "stremio movies",
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
            if (!a) {
                out->message = i18n::tr("stremio.not_connected");
                return;
            }
            std::string err;
            bool ok = crawl<Movie>(a->base, a->manifest, "movie", token, categories, movies, err,
                                   [](const std::string &b, const std::string &cat, std::vector<Movie> &got, int &n) {
                                       return stremio::parseMovies(b, cat, got, n);
                                   }, [](const Movie &m) { return m.streamId; });
            if (token.canceled()) {
                return;
            }
            if (!ok) {
                out->message = err;
                return;
            }
            out->savedAt = clockx::unixNow();
            if (!movies.empty()) {
                cache.save(profileId, CACHE_MOVIES, mediabrowser::encodeMovies(categories, movies), out->savedAt);
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

CancelToken StremioService::loadSeriesList(const Profile &profile, const std::string &dataDir, sources::Source source,
                                           std::function<void(sources::SeriesListOutcome &)> callback) {
    auto out = std::make_shared<sources::SeriesListOutcome>();
    std::shared_ptr<const Addon> a = addonFor(profile);
    std::string profileId = profile.id;
    bool fromCache = source == sources::Source::Cache;
    return jobs.submit(fromCache ? JobPriority::High : JobPriority::Normal, fromCache ? "stremio series (cache)"
                                                                                     : "stremio series",
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
            if (!a) {
                out->message = i18n::tr("stremio.not_connected");
                return;
            }
            std::string err;
            bool ok = crawl<Series>(a->base, a->manifest, "series", token, categories, series, err,
                                    [](const std::string &b, const std::string &cat, std::vector<Series> &got, int &n) {
                                        return stremio::parseSeries(b, cat, got, n);
                                    }, [](const Series &s) { return s.seriesId; });
            if (token.canceled()) {
                return;
            }
            if (!ok) {
                out->message = err;
                return;
            }
            out->savedAt = clockx::unixNow();
            if (!series.empty()) {
                cache.save(profileId, CACHE_SERIES, mediabrowser::encodeSeries(categories, series), out->savedAt);
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

CancelToken StremioService::loadMovieInfo(const Profile &profile, const std::string &id,
                                          std::function<void(sources::InfoOutcome<MovieInfo> &)> callback) {
    auto out = std::make_shared<sources::InfoOutcome<MovieInfo>>();
    std::shared_ptr<const Addon> a = addonFor(profile);
    return jobs.submit(JobPriority::High, "stremio meta", [out, a, id](const CancelToken &token) {
        if (!a) {
            out->message = i18n::tr("stremio.not_connected");
            return;
        }
        out->info.streamId = id;
        if (!a->manifest.meta) {
            out->ok = true;   // no details: the list entry is all there is (playing still works)
            return;
        }
        http::Response r = get(stremio::metaUrl(a->base, "movie", id), token, 20000);
        if (token.canceled()) {
            return;
        }
        out->ok = r.ok() && stremio::parseMovieMeta(r.body, out->info);
        if (!out->ok) {
            out->message = r.ok() ? i18n::tr("stremio.unreadable") : http::describe(r);
        }
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StremioService::loadSeriesInfo(const Profile &profile, const std::string &id,
                                           std::function<void(sources::InfoOutcome<SeriesInfo> &)> callback) {
    auto out = std::make_shared<sources::InfoOutcome<SeriesInfo>>();
    std::shared_ptr<const Addon> a = addonFor(profile);
    return jobs.submit(JobPriority::High, "stremio series meta", [out, a, id](const CancelToken &token) {
        if (!a) {
            out->message = i18n::tr("stremio.not_connected");
            return;
        }
        if (!a->manifest.meta) {
            out->message = i18n::tr("stremio.no_details");   // a series needs its episode list
            return;
        }
        http::Response r = get(stremio::metaUrl(a->base, "series", id), token, 20000);
        if (token.canceled()) {
            return;
        }
        out->ok = r.ok() && stremio::parseSeriesMeta(r.body, out->info);
        if (!out->ok) {
            out->message = r.ok() ? i18n::tr("stremio.unreadable") : http::describe(r);
        }
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StremioService::resolve(const Profile &profile, bool episode, const std::string &id,
                                    sources::LinkCallback callback) {
    auto out = std::make_shared<sources::LinkOutcome>();
    std::shared_ptr<const Addon> a = addonFor(profile);
    return jobs.submit(JobPriority::High, "stremio stream", [out, a, episode, id](const CancelToken &token) {
        if (!a) {
            out->message = i18n::tr("stremio.not_connected");
            return;
        }
        http::Response r = get(stremio::streamUrl(a->base, episode ? "series" : "movie", id), token, 30000);
        if (token.canceled()) {
            return;
        }
        stremio::Streams s;
        if (!r.ok() || !stremio::parseStreams(r.body, s)) {
            out->message = r.ok() ? i18n::tr("stremio.unreadable") : http::describe(r);
            return;
        }
        LOG_I("stremio", "streams: %zu playable, %d skipped (torrent / YouTube / external / extra headers)",
              s.playable.size(), s.skipped);
        if (s.playable.empty()) {
            out->message = i18n::tr("stremio.no_stream");
            return;
        }
        out->ok = true;
        out->url = s.playable[0].url;
        out->userAgent = s.playable[0].userAgent;
    }, [out, callback]() {
        callback(*out);
    });
}
