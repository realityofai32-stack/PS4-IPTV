#include <algorithm>
#include <set>
#include <unordered_set>

#include "stalker_service.h"
#include "../core/url.h"
#include "../i18n/i18n.h"
#include "../iptv/xtream.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/log.h"
#include "../platform/redact.h"
#include "../player/protocols.h"
#include "../storage/catalog_cache.h"

using namespace iptv;

// one portal session (shared by the jobs of a profile)
struct StalkerService::Session {
    std::string profileId;
    std::string server;
    std::string mac;
    stalker::Portal portal;
    stalker::Identity id;
    std::mutex handshakeMutex;   // one handshake at a time
    std::mutex m;                // endpoint / token / generation
    std::string endpoint;        // the API address that answered the handshake
    std::string token;
    unsigned generation = 0;     // +1 per handshake
};

// list entries of the signed-in portal: details and play commands (any thread, under the mutex)
struct StalkerService::Store {
    std::mutex m;
    std::string profileId;
    std::unordered_map<std::string, stalker::VodEntry> movies;   // by movie id
    std::unordered_map<std::string, stalker::VodEntry> series;   // by series id ("v:<id>" for VOD-module series)
    bool moviesFromCacheTried = false;
    bool seriesFromCacheTried = false;
};

namespace {

    const char *const CACHE_LIVE = "stalker_live";
    const char *const CACHE_LIVE_CATS = "stalker_live_categories";
    const char *const CACHE_VOD = "stalker_vod";
    const char *const CACHE_VOD_CATS = "stalker_vod_categories";
    const char *const CACHE_VSERIES = "stalker_vseries";          // VOD entries with is_series (by-product of Movies)
    const char *const CACHE_SERIES = "stalker_series";
    const char *const CACHE_SERIES_CATS = "stalker_series_categories";
    const int LIVE_MAX_PAGES = 400;
    const int SEASON_MAX_PAGES = 20;

    enum class Fail {
        None,
        Network,      // see `response`
        NotPortal,    // no candidate address answered like a portal
        Refused,      // get_profile: MAC not allowed / blocked
        NeedsLogin,   // get_profile: login + password required (not supported)
        Unexpected,   // a response that is not what the action returns
        Canceled
    };

    struct Call {
        Fail fail = Fail::None;
        http::Response response;
        std::string portalMessage;   // block_msg / msg of get_profile
    };

    std::string originOf(const StalkerService::Session &s) {
        return s.portal.origin;
    }

    http::Request baseRequest(const StalkerService::Session &s, const std::string &url, const std::string &token,
                              const CancelToken &cancel) {
        http::Request req;
        req.url = url;
        req.cancel = cancel.shared();
        req.userAgent = stalker::USER_AGENT;
        req.headers = stalker::headers(s.id, s.portal.referer, token);
        req.totalTimeoutMs = 30000;
        req.maxBytes = 8u * 1024 * 1024;
        return req;
    }

    bool networkFailure(const http::Response &r) {
        return r.error != http::Error::None;
    }

    // handshake + get_profile. staleGeneration: the generation the caller saw fail (only one new handshake for all
    // the jobs that hit the same expired token)
    Call handshake(StalkerService::Session &s, const CancelToken &cancel, unsigned staleGeneration) {
        std::lock_guard<std::mutex> serial(s.handshakeMutex);
        {
            std::lock_guard<std::mutex> lock(s.m);
            if (s.generation != staleGeneration && !s.token.empty()) {
                return {};   // another job renewed it meanwhile
            }
        }
        Call out;
        std::vector<std::string> candidates;
        {
            std::lock_guard<std::mutex> lock(s.m);
            if (!s.endpoint.empty()) {
                candidates.push_back(s.endpoint);
            }
        }
        for (const auto &e: s.portal.endpoints) {
            if (std::find(candidates.begin(), candidates.end(), e) == candidates.end()) {
                candidates.push_back(e);
            }
        }
        out.fail = Fail::NotPortal;
        for (const auto &endpoint: candidates) {
            if (cancel.canceled()) {
                out.fail = Fail::Canceled;
                return out;
            }
            http::Request req = baseRequest(s, stalker::apiUrl(endpoint, "stb", "handshake", "&token=&prehash="), "",
                                            cancel);
            req.totalTimeoutMs = 20000;
            req.maxBytes = 256 * 1024;
            http::Response r = http::get(req);
            if (networkFailure(r)) {
                out.fail = r.error == http::Error::Canceled ? Fail::Canceled : Fail::Network;
                out.response = r;
                if (r.error == http::Error::Dns || r.error == http::Error::Connect || r.error == http::Error::Tls
                    || r.error == http::Error::Timeout || r.error == http::Error::Canceled) {
                    return out;   // the same host for every candidate: no point in trying the others
                }
                continue;
            }
            std::string token, why;
            if (!r.ok() || !stalker::parseToken(r.body, token, why)) {
                LOG_I("stalker", "no portal API at %s (HTTP %ld%s%s)", protocols::origin(endpoint).c_str(), r.status,
                      why.empty() ? "" : ", ", why.c_str());
                out.response = r;
                continue;
            }
            redact::addSecret(token);
            // get_profile with the new token: the portal checks the box (MAC, device ids) here
            http::Request preq = baseRequest(s, stalker::apiUrl(endpoint, "stb", "get_profile",
                                                                stalker::profileParams(s.id, clockx::unixNow())),
                                             token, cancel);
            http::Response pr = http::get(preq);
            if (networkFailure(pr)) {
                out.fail = pr.error == http::Error::Canceled ? Fail::Canceled : Fail::Network;
                out.response = pr;
                return out;
            }
            stalker::ProfileReply profile = stalker::parseProfile(pr.body);
            size_t pathAt = endpoint.find('/', endpoint.find("://") + 3);
            std::string path = pathAt == std::string::npos ? "/" : endpoint.substr(pathAt);
            if (profile.needsLogin || profile.blocked) {
                out.fail = profile.needsLogin ? Fail::NeedsLogin : Fail::Refused;
                out.portalMessage = profile.message;
                LOG_W("stalker", "portal %s%s refused this box: %s", protocols::origin(endpoint).c_str(), path.c_str(),
                      profile.needsLogin ? "login required" : "not allowed / blocked");
                return out;
            }
            if (!profile.ok) {
                LOG_W("stalker", "get_profile answer not understood (HTTP %ld): continuing with the token", pr.status);
            }
            {
                std::lock_guard<std::mutex> lock(s.m);
                s.endpoint = endpoint;
                s.token = token;
                s.generation++;
            }
            LOG_I("stalker", "session started with %s%s", protocols::origin(endpoint).c_str(), path.c_str());
            out = Call();
            return out;
        }
        return out;
    }

    // one API request with the session token; an unauthorized answer renews the session once
    Call request(StalkerService::Session &s, const std::string &type, const std::string &action,
                 const std::string &params, const CancelToken &cancel, size_t maxBytes = 8u * 1024 * 1024,
                 long timeoutMs = 30000) {
        for (int attempt = 0; attempt < 2; attempt++) {
            std::string endpoint, token;
            unsigned gen;
            {
                std::lock_guard<std::mutex> lock(s.m);
                endpoint = s.endpoint;
                token = s.token;
                gen = s.generation;
            }
            if (token.empty()) {
                Call h = handshake(s, cancel, gen);
                if (h.fail != Fail::None) {
                    return h;
                }
                continue;
            }
            http::Request req = baseRequest(s, stalker::apiUrl(endpoint, type, action, params), token, cancel);
            req.maxBytes = maxBytes;
            req.totalTimeoutMs = timeoutMs;
            req.stallTimeoutS = 30;
            Call c;
            c.response = http::get(req);
            if (networkFailure(c.response)) {
                c.fail = c.response.error == http::Error::Canceled ? Fail::Canceled : Fail::Network;
                return c;
            }
            json::Value js;
            bool unauthorized = c.response.status == 401 || c.response.status == 403
                                || (c.response.ok() && stalker::envelope(c.response.body, js) == stalker::Reply::AuthFailed);
            if (unauthorized && attempt == 0) {
                LOG_I("stalker", "%s/%s: session expired, signing in again", type.c_str(), action.c_str());
                {
                    std::lock_guard<std::mutex> lock(s.m);
                    if (s.generation == gen) {
                        s.token.clear();
                    }
                }
                Call h = handshake(s, cancel, gen);
                if (h.fail != Fail::None) {
                    return h;
                }
                continue;
            }
            if (!c.response.ok()) {
                c.fail = Fail::Unexpected;
            }
            return c;
        }
        Call c;
        c.fail = Fail::Unexpected;
        return c;
    }

    std::string describe(const Call &c) {
        switch (c.fail) {
            case Fail::Network:
                return http::describe(c.response);
            case Fail::NotPortal:
                return i18n::tr("stalker.not_portal");
            case Fail::Refused:
                return i18n::tr("stalker.refused");
            case Fail::NeedsLogin:
                return i18n::tr("stalker.needs_login");
            case Fail::Canceled:
                return i18n::tr("net.canceled");
            default:
                return c.response.status != 0 && !c.response.ok() ? http::describe(c.response)
                                                                    : i18n::tr("stalker.unexpected");
        }
    }

    // ------------------------------------------------------------------ lists
    struct Crawl {
        std::vector<Category> categories;
        std::vector<stalker::VodEntry> entries;
        int pages = 0;
        bool truncated = false;
        Call failure;            // why nothing could be read
    };

    // every page of every category of a VOD / series list (one request at a time, cancellable)
    bool crawl(StalkerService::Session &s, const std::string &type, const CancelToken &cancel,
               std::atomic<int> &progress, Crawl &out) {
        Call c = request(s, type, "get_categories", "", cancel);
        std::string err;
        if (c.fail != Fail::None || !stalker::parseCategories(c.response.body, out.categories, err)) {
            if (c.fail == Fail::None) {
                c.fail = Fail::Unexpected;
                LOG_W("stalker", "%s categories: %s", type.c_str(), err.c_str());
            }
            out.failure = c;
            return false;
        }
        std::vector<Category> lists = out.categories;
        if (lists.empty()) {
            lists.push_back({"*", "", ""});   // a portal without categories: the whole list
        }
        std::unordered_set<std::string> seen;
        bool anyPage = false;
        for (const auto &cat: lists) {
            for (int page = 1; page <= StalkerService::MAX_PAGES; page++) {
                if (cancel.canceled()) {
                    out.failure.fail = Fail::Canceled;
                    return false;
                }
                if (out.pages >= StalkerService::MAX_PAGES) {
                    out.truncated = true;
                    break;
                }
                Call pc = request(s, type, "get_ordered_list",
                                  "&category=" + url::encode(cat.id) + "&genre=*&p=" + std::to_string(page)
                                  + "&sortby=added&fav=0&hd=0&not_ended=0&abc=*", cancel);
                out.pages++;
                stalker::Page info;
                std::vector<stalker::VodEntry> entries;
                if (pc.fail != Fail::None || !stalker::parseVodPage(pc.response.body, originOf(s), entries, info, err)) {
                    if (pc.fail == Fail::Canceled) {
                        out.failure = pc;
                        return false;
                    }
                    if (!anyPage) {
                        out.failure = pc.fail == Fail::None ? Call{Fail::Unexpected, pc.response, ""} : pc;
                    }
                    LOG_W("stalker", "%s category %s page %d unreadable (%s): next category", type.c_str(),
                          cat.id.c_str(), page, pc.fail == Fail::None ? err.c_str() : "request failed");
                    break;
                }
                anyPage = true;
                size_t before = out.entries.size();
                int repeated = 0;
                for (auto &e: entries) {
                    if (!seen.insert(e.id).second) {
                        repeated++;
                        continue;   // listed in an earlier category (or page): the first one keeps it
                    }
                    if (cat.id != "*") {
                        e.categoryId = cat.id;
                    } else if (e.categoryId.empty()) {
                        e.categoryId = "";
                    }
                    out.entries.push_back(std::move(e));
                }
                progress = (int) out.entries.size();
                int done = (page - 1) * std::max(1, info.perPage) + info.count;
                bool nothingNew = out.entries.size() == before && repeated > 0 && page > 1;   // a portal ignoring "p"
                if (info.count == 0 || nothingNew || (info.totalItems >= 0 && done >= info.totalItems)
                    || (info.totalItems < 0 && info.perPage > 0 && info.count < info.perPage)) {
                    break;
                }
            }
            if (out.truncated) {
                break;
            }
        }
        if (!anyPage) {
            return false;
        }
        if (lists.size() == 1 && lists[0].id == "*") {
            out.categories.clear();   // entries keep their own category ids (Uncategorized when unknown)
        }
        return true;
    }

    template<typename Catalog, typename Item>
    std::shared_ptr<Catalog> buildCatalog(std::vector<Category> categories, std::vector<Item> items, int64_t savedAt,
                                          bool fromCache, int raw) {
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
        d.parse.raw = raw;
        d.parse.parsed = parsed;
        d.cached = parsed;
        d.fromCache = fromCache;
        d.parseMs = (t1 - t0) * 1000;
        d.indexMs = (t2 - t1) * 1000;
        d.sortMs = (t3 - t2) * 1000;
        return catalog;
    }

    void remember(StalkerService::Store &store, const std::string &profileId, bool movies,
                  const std::vector<stalker::VodEntry> &entries) {
        std::lock_guard<std::mutex> lock(store.m);
        if (store.profileId != profileId) {
            store.profileId = profileId;
            store.movies.clear();
            store.series.clear();
            store.moviesFromCacheTried = false;
            store.seriesFromCacheTried = false;
        }
        auto &map = movies ? store.movies : store.series;
        for (const auto &e: entries) {
            map[e.id] = e;
        }
    }

    // the saved list of a profile into the store (detail / play requests after a restart, before a section opened)
    void rememberFromCache(StalkerService::Store &store, const std::string &dataDir, const std::string &profileId,
                           bool movies) {
        {
            std::lock_guard<std::mutex> lock(store.m);
            bool &tried = movies ? store.moviesFromCacheTried : store.seriesFromCacheTried;
            if (store.profileId == profileId && tried) {
                return;
            }
        }
        CatalogCache cache(dataDir);
        std::vector<std::string> names = movies ? std::vector<std::string>{CACHE_VOD}
                                                : std::vector<std::string>{CACHE_SERIES, CACHE_VSERIES};
        for (const auto &name: names) {
            std::string body, err;
            int64_t savedAt = 0;
            std::vector<stalker::VodEntry> entries;
            if (cache.load(profileId, name, body, savedAt) && stalker::decodeEntries(body, entries, err)) {
                if (name == CACHE_VSERIES) {
                    for (auto &e: entries) {
                        e.id = "v:" + e.id;
                    }
                }
                remember(store, profileId, movies, entries);
            }
        }
        std::lock_guard<std::mutex> lock(store.m);
        (movies ? store.moviesFromCacheTried : store.seriesFromCacheTried) = true;
        store.profileId = profileId;
    }

    bool lookup(StalkerService::Store &store, const std::string &profileId, bool movies, const std::string &id,
                stalker::VodEntry &out) {
        std::lock_guard<std::mutex> lock(store.m);
        if (store.profileId != profileId) {
            return false;
        }
        auto &map = movies ? store.movies : store.series;
        auto it = map.find(id);
        if (it == map.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    // create_link for a channel (itv) or a movie / episode (vod)
    sources::LinkOutcome createLink(StalkerService::Session &s, const std::string &type, const std::string &cmd,
                                    int series, const CancelToken &cancel) {
        sources::LinkOutcome out;
        out.userAgent = stalker::USER_AGENT;
        Call c = request(s, type, "create_link",
                         "&cmd=" + url::encode(cmd) + "&series=" + (series > 0 ? std::to_string(series) : "")
                         + "&forced_storage=undefined&disable_ad=0&download=0", cancel, 512 * 1024, 20000);
        std::string url, err;
        if (c.fail == Fail::None && stalker::parseLink(c.response.body, url, err)) {
            redact::addUrl(url);   // play_token / mac in the query
            out.ok = true;
            out.url = url;
            LOG_I("stalker", "%s link created: %s", type.c_str(), protocols::origin(url).c_str());
            return out;
        }
        if (c.fail == Fail::Canceled) {
            out.message = i18n::tr("net.canceled");
            return out;
        }
        // the command itself may already be a complete stream address
        std::string direct = stalker::urlOfCmd(cmd);
        if (stalker::cmdHasUrl(cmd) && c.fail != Fail::Network) {
            redact::addUrl(direct);
            out.ok = true;
            out.url = direct;
            LOG_W("stalker", "%s create_link failed (%s): playing the command's own address", type.c_str(),
                  c.fail == Fail::None ? err.c_str() : "request failed");
            return out;
        }
        out.message = c.fail == Fail::None ? i18n::tr(stalker::linkErrorKey(err).c_str()) : describe(c);
        LOG_W("stalker", "%s create_link failed: %s", type.c_str(), c.fail == Fail::None ? err.c_str() : "request failed");
        return out;
    }
}

StalkerService::StalkerService(JobSystem &j, std::string dataDir) : jobs(j), dir(std::move(dataDir)),
                                                                    store(std::make_shared<Store>()) {}

std::shared_ptr<StalkerService::Session> StalkerService::sessionFor(const Profile &profile) {
    auto it = sessions.find(profile.id);
    if (it != sessions.end() && it->second->server == profile.server && it->second->mac == profile.mac) {
        return it->second;
    }
    auto s = std::make_shared<Session>();
    s->profileId = profile.id;
    s->server = profile.server;
    s->mac = profile.mac;
    s->portal = stalker::portalFor(profile.server);
    std::string mac;
    if (!stalker::normalizeMac(profile.mac, mac)) {
        mac = profile.mac;
    }
    s->id = stalker::identity(mac);
    redact::addMac(mac);
    for (const auto &v: {s->id.serial, s->id.deviceId, s->id.deviceId2, s->id.signature}) {
        redact::addSecret(v);
    }
    if (!profile.id.empty()) {
        sessions[profile.id] = s;
    }
    return s;
}

int StalkerService::progress(bool movies) const {
    return movies ? movieProgress->load() : seriesProgress->load();
}

CancelToken StalkerService::authenticate(const Profile &profile, sources::AuthCallback callback) {
    auto out = std::make_shared<sources::AuthOutcome>();
    std::shared_ptr<Session> s = sessionFor(profile);
    return jobs.submit(JobPriority::High, "stalker auth", [s, out](const CancelToken &token) {
        if (!s->portal.ok) {
            out->result.status = AuthStatus::Malformed;
            out->message = i18n::tr(s->portal.error.c_str());
            return;
        }
        // a fresh sign-in: always a new handshake
        unsigned gen;
        {
            std::lock_guard<std::mutex> lock(s->m);
            s->token.clear();
            gen = s->generation;
        }
        double t0 = clockx::monotonic();
        Call h = handshake(*s, token, gen);
        if (h.fail != Fail::None) {
            out->result.status = h.fail == Fail::Network ? AuthStatus::Network
                                 : h.fail == Fail::NotPortal ? AuthStatus::Malformed
                                 : h.fail == Fail::NeedsLogin ? AuthStatus::Disabled
                                 : h.fail == Fail::Refused ? AuthStatus::InvalidCredentials : AuthStatus::Malformed;
            out->message = describe(h);
            out->result.account.message = h.portalMessage;
            LOG_W("stalker", "sign-in to %s failed: %s (%s)", s->portal.origin.c_str(), out->message.c_str(),
                  h.response.detail.c_str());
            return;
        }
        Call info = request(*s, "account_info", "get_main_info", "", token, 256 * 1024, 15000);
        if (info.fail == Fail::None) {
            stalker::parseAccountInfo(info.response.body, out->result.account);
        }
        out->result.status = AuthStatus::Ok;
        if (out->result.account.status.empty()) {
            out->result.account.status = "Active";
        }
        out->message = xtream::authStatusText(AuthStatus::Ok);
        LOG_I("stalker", "signed in to %s in %.2fs%s", s->portal.origin.c_str(), clockx::monotonic() - t0,
              out->result.account.expiresAt > 0 ? ", expiry date known" : "");
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StalkerService::loadLive(const Profile &profile, const std::string &dataDir, sources::LiveCallback callback,
                                     bool cacheOnly) {
    auto out = std::make_shared<sources::LiveOutcome>();
    std::shared_ptr<Session> s = sessionFor(profile);
    std::string profileId = profile.id;
    return jobs.submit(JobPriority::High, "stalker live", [s, out, dataDir, cacheOnly, profileId](const CancelToken &token) {
        CatalogCache cache(dataDir);
        std::string why;
        if (!cacheOnly) {
            double t0 = clockx::monotonic();
            Call cats = request(*s, "itv", "get_genres", "", token);
            std::string err;
            bool ok = cats.fail == Fail::None && stalker::parseCategories(cats.response.body, out->categories, err);
            Call list;
            if (ok) {
                list = request(*s, "itv", "get_all_channels", "", token, 64u * 1024 * 1024, 120000);
                ok = list.fail == Fail::None
                     && stalker::parseChannels(list.response.body, s->portal.origin, out->channels, nullptr, err);
                if (!ok || out->channels.empty()) {
                    // portals without get_all_channels: the paged list of every genre
                    LOG_I("stalker", "get_all_channels unavailable (%s): reading the paged channel list",
                          list.fail == Fail::None ? err.c_str() : "request failed");
                    out->channels.clear();
                    ok = false;
                    for (int page = 1; page <= LIVE_MAX_PAGES && !token.canceled(); page++) {
                        Call pc = request(*s, "itv", "get_ordered_list", "&genre=*&fav=0&sortby=number&p="
                                                                         + std::to_string(page), token);
                        stalker::Page info;
                        size_t before = out->channels.size();
                        if (pc.fail != Fail::None
                            || !stalker::parseChannels(pc.response.body, s->portal.origin, out->channels, &info, err)) {
                            list = pc;
                            break;
                        }
                        ok = true;
                        int done = (page - 1) * std::max(1, info.perPage) + info.count;
                        if (info.count == 0 || out->channels.size() == before
                            || (info.totalItems >= 0 && done >= info.totalItems)) {
                            break;
                        }
                    }
                }
            }
            if (token.canceled()) {
                return;
            }
            if (ok && !out->channels.empty()) {
                out->ok = true;
                int64_t now = clockx::unixNow();
                std::string cacheErr;
                if (!cache.save(profileId, CACHE_LIVE, stalker::encodeChannels(out->channels), now,
                                (int) out->channels.size(), &cacheErr)
                    || !cache.save(profileId, CACHE_LIVE_CATS, stalker::encodeCategories(out->categories), now, -1,
                                   &cacheErr)) {
                    LOG_W("stalker", "live cache not saved: %s", cacheErr.c_str());
                }
                LOG_I("stalker", "live: %d genres, %d channels (%.1fs)", (int) out->categories.size(),
                      (int) out->channels.size(), clockx::monotonic() - t0);
                return;
            }
            const Call &bad = cats.fail != Fail::None ? cats : list;
            why = bad.fail == Fail::None ? i18n::tr("live.list_unreadable") : describe(bad);
            LOG_W("stalker", "live list from the portal failed: %s", why.c_str());
        } else {
            why = i18n::tr("connect.offline_mode");
        }
        out->categories.clear();
        out->channels.clear();
        std::string catBody, listBody, err;
        int64_t savedCats = 0, savedList = 0;
        if (cache.load(profileId, CACHE_LIVE_CATS, catBody, savedCats) && cache.load(profileId, CACHE_LIVE, listBody, savedList)
            && stalker::decodeCategories(catBody, out->categories, err)
            && stalker::decodeChannels(listBody, out->channels, err)) {
            out->ok = true;
            out->fromCache = true;
            out->savedAt = savedList;
            out->message = i18n::tr("live.showing_saved", {why});
            LOG_I("stalker", "live: using the saved list (%d channels)", (int) out->channels.size());
        } else {
            out->message = why;
        }
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StalkerService::loadMovies(const Profile &profile, const std::string &dataDir, sources::Source source,
                                       std::function<void(sources::MoviesOutcome &)> callback) {
    auto out = std::make_shared<sources::MoviesOutcome>();
    std::shared_ptr<Session> s = sessionFor(profile);
    std::shared_ptr<Store> st = store;
    auto progress = movieProgress;
    std::string profileId = profile.id;
    bool fromCache = source == sources::Source::Cache;
    return jobs.submit(fromCache ? JobPriority::High : JobPriority::Normal, fromCache ? "stalker movies (cache)"
                                                                                     : "stalker movies",
                       [=](const CancelToken &token) {
        CatalogCache cache(dataDir);
        std::vector<Category> categories;
        std::vector<stalker::VodEntry> entries;
        std::string err;
        if (fromCache) {
            std::string catBody, listBody;
            CatalogCache::Meta catMeta, listMeta;
            if (!cache.load(profileId, CACHE_VOD_CATS, catBody, catMeta) || !cache.load(profileId, CACHE_VOD, listBody, listMeta)
                || catMeta.savedAt != listMeta.savedAt || !stalker::decodeCategories(catBody, categories, err)
                || !stalker::decodeEntries(listBody, entries, err)) {
                out->message = i18n::tr("catalog.no_saved_copy");
                return;
            }
            out->savedAt = listMeta.savedAt;
        } else {
            double t0 = clockx::monotonic();
            *progress = 0;
            Crawl c;
            bool ok = crawl(*s, "vod", token, *progress, c);
            *progress = 0;
            if (token.canceled()) {
                return;
            }
            if (!ok) {
                out->message = describe(c.failure);
                LOG_W("stalker", "movies from the portal failed: %s", out->message.c_str());
                return;
            }
            std::vector<stalker::VodEntry> vseries;
            for (auto &e: c.entries) {
                (e.isSeries ? vseries : entries).push_back(std::move(e));
            }
            categories = std::move(c.categories);
            out->savedAt = clockx::unixNow();
            std::string cacheErr;
            if (!cache.save(profileId, CACHE_VOD, stalker::encodeEntries(entries), out->savedAt, (int) entries.size(),
                            &cacheErr)
                || !cache.save(profileId, CACHE_VOD_CATS, stalker::encodeCategories(categories), out->savedAt, -1,
                               &cacheErr)
                || !cache.save(profileId, CACHE_VSERIES, stalker::encodeEntries(vseries), out->savedAt,
                               (int) vseries.size(), &cacheErr)) {
                LOG_W("stalker", "movies cache not saved: %s", cacheErr.c_str());
            }
            LOG_I("stalker", "movies: %d categories, %d movies, %d VOD series, %d pages in %.1fs%s",
                  (int) categories.size(), (int) entries.size(), (int) vseries.size(), c.pages,
                  clockx::monotonic() - t0, c.truncated ? " (page limit reached: list incomplete)" : "");
            if (entries.empty()) {
                out->message = i18n::tr("catalog.movies_empty");
                return;
            }
        }
        remember(*st, profileId, true, entries);
        std::vector<Movie> movies;
        movies.reserve(entries.size());
        for (const auto &e: entries) {
            movies.push_back(stalker::toMovie(e));
        }
        int raw = (int) movies.size();
        out->catalog = buildCatalog<MovieCatalog>(std::move(categories), std::move(movies), out->savedAt, fromCache, raw);
        out->ok = true;
        LOG_I("stalker", "movies%s: %d categories, %d items", fromCache ? " from the saved copy" : "",
              (int) out->catalog->categories().size(), (int) out->catalog->size());
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StalkerService::loadSeriesList(const Profile &profile, const std::string &dataDir, sources::Source source,
                                           std::function<void(sources::SeriesListOutcome &)> callback) {
    auto out = std::make_shared<sources::SeriesListOutcome>();
    std::shared_ptr<Session> s = sessionFor(profile);
    std::shared_ptr<Store> st = store;
    auto progress = seriesProgress;
    std::string profileId = profile.id;
    bool fromCache = source == sources::Source::Cache;
    return jobs.submit(fromCache ? JobPriority::High : JobPriority::Normal, fromCache ? "stalker series (cache)"
                                                                                     : "stalker series",
                       [=](const CancelToken &token) {
        CatalogCache cache(dataDir);
        std::vector<Category> categories;
        std::vector<stalker::VodEntry> entries;   // ids are the Series ids ("v:<id>" for VOD-module series)
        std::string err;
        if (fromCache) {
            std::string catBody, listBody;
            CatalogCache::Meta catMeta, listMeta;
            if (!cache.load(profileId, CACHE_SERIES_CATS, catBody, catMeta)
                || !cache.load(profileId, CACHE_SERIES, listBody, listMeta) || catMeta.savedAt != listMeta.savedAt
                || !stalker::decodeCategories(catBody, categories, err) || !stalker::decodeEntries(listBody, entries, err)) {
                out->message = i18n::tr("catalog.no_saved_copy");
                return;
            }
            out->savedAt = listMeta.savedAt;
        } else {
            double t0 = clockx::monotonic();
            *progress = 0;
            Crawl c;
            bool ok = crawl(*s, "series", token, *progress, c);
            const char *origin = "series module";
            if (!ok && !token.canceled() && c.failure.fail != Fail::Network) {
                // no series module: series are VOD entries with is_series (saved with Movies, else read now)
                LOG_I("stalker", "series module unavailable: series from the VOD list");
                origin = "VOD list";
                std::string body;
                int64_t savedAt = 0;
                Crawl v;
                if (cache.load(profileId, CACHE_VSERIES, body, savedAt) && stalker::decodeEntries(body, v.entries, err)
                    && !v.entries.empty()) {
                    std::string catBody;
                    int64_t catSaved = 0;
                    if (cache.load(profileId, CACHE_VOD_CATS, catBody, catSaved)) {
                        stalker::decodeCategories(catBody, v.categories, err);
                    }
                    ok = true;
                } else {
                    v = Crawl();
                    ok = crawl(*s, "vod", token, *progress, v);
                }
                std::vector<stalker::VodEntry> kept;
                for (auto &e: v.entries) {
                    if (e.isSeries) {
                        e.id = "v:" + e.id;
                        kept.push_back(std::move(e));
                    }
                }
                v.entries = std::move(kept);
                c.categories = std::move(v.categories);
                c.entries = std::move(v.entries);
                c.pages += v.pages;
                c.truncated |= v.truncated;
                if (!ok) {
                    c.failure = v.failure;
                }
            }
            *progress = 0;
            if (token.canceled()) {
                return;
            }
            if (!ok) {
                out->message = describe(c.failure);
                LOG_W("stalker", "series from the portal failed: %s", out->message.c_str());
                return;
            }
            entries = std::move(c.entries);
            categories = std::move(c.categories);
            out->savedAt = clockx::unixNow();
            std::string cacheErr;
            if (!cache.save(profileId, CACHE_SERIES, stalker::encodeEntries(entries), out->savedAt, (int) entries.size(),
                            &cacheErr)
                || !cache.save(profileId, CACHE_SERIES_CATS, stalker::encodeCategories(categories), out->savedAt, -1,
                               &cacheErr)) {
                LOG_W("stalker", "series cache not saved: %s", cacheErr.c_str());
            }
            LOG_I("stalker", "series (%s): %d categories, %d series, %d pages in %.1fs%s", origin,
                  (int) categories.size(), (int) entries.size(), c.pages, clockx::monotonic() - t0,
                  c.truncated ? " (page limit reached: list incomplete)" : "");
            if (entries.empty()) {
                out->message = i18n::tr("catalog.series_empty");
                return;
            }
        }
        remember(*st, profileId, false, entries);
        std::vector<Series> list;
        list.reserve(entries.size());
        for (const auto &e: entries) {
            list.push_back(stalker::toSeries(e, e.id));
        }
        int raw = (int) list.size();
        out->catalog = buildCatalog<SeriesCatalog>(std::move(categories), std::move(list), out->savedAt, fromCache, raw);
        out->ok = true;
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StalkerService::loadMovieInfo(const Profile &profile, const std::string &streamId,
                                          std::function<void(sources::InfoOutcome<MovieInfo> &)> callback) {
    auto out = std::make_shared<sources::InfoOutcome<MovieInfo>>();
    std::shared_ptr<Store> st = store;
    std::string profileId = profile.id;
    std::string dataDir = dir;
    return jobs.submit(JobPriority::High, "stalker movie info", [=](const CancelToken &) {
        stalker::VodEntry e;
        if (!lookup(*st, profileId, true, streamId, e)) {
            rememberFromCache(*st, dataDir, profileId, true);
        }
        if (!lookup(*st, profileId, true, streamId, e)) {
            out->message = i18n::tr("detail.unreadable");
            return;
        }
        out->info = stalker::toMovieInfo(e);
        out->ok = true;
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StalkerService::loadSeriesInfo(const Profile &profile, const std::string &seriesId,
                                           std::function<void(sources::InfoOutcome<SeriesInfo> &)> callback) {
    auto out = std::make_shared<sources::InfoOutcome<SeriesInfo>>();
    std::shared_ptr<Session> s = sessionFor(profile);
    std::shared_ptr<Store> st = store;
    std::string profileId = profile.id;
    std::string dataDir = dir;
    return jobs.submit(JobPriority::High, "stalker series info", [=](const CancelToken &token) {
        stalker::VodEntry e;
        bool known = lookup(*st, profileId, false, seriesId, e);
        if (!known) {
            rememberFromCache(*st, dataDir, profileId, false);
            known = lookup(*st, profileId, false, seriesId, e);
        }
        if (seriesId.compare(0, 2, "v:") == 0) {
            // VOD-module series: the episode numbers are in the list entry
            if (!known) {
                out->message = i18n::tr("detail.unreadable");
                return;
            }
            out->info = stalker::seriesFromEntry(e, seriesId);
            out->ok = true;
            return;
        }
        // series module: seasons, then the episodes of each season
        SeriesInfo info;
        info.seriesId = seriesId;
        if (known) {
            info.series = stalker::toSeries(e, seriesId);
        } else {
            info.series.seriesId = seriesId;
        }
        std::vector<stalker::SeasonEntry> seasons;
        std::string err;
        for (int page = 1; page <= SEASON_MAX_PAGES; page++) {
            Call c = request(*s, "series", "get_ordered_list", "&movie_id=" + url::encode(seriesId)
                                                               + "&season_id=0&episode_id=0&p=" + std::to_string(page), token);
            stalker::Page pinfo;
            size_t before = seasons.size();
            if (c.fail != Fail::None || !stalker::parseSeasons(c.response.body, seasons, pinfo, err)) {
                if (seasons.empty()) {
                    out->message = c.fail == Fail::None ? i18n::tr("detail.unreadable") : describe(c);
                    LOG_W("stalker", "seasons of %s: %s", seriesId.c_str(), c.fail == Fail::None ? err.c_str() : "failed");
                    return;
                }
                break;
            }
            int done = (page - 1) * std::max(1, pinfo.perPage) + pinfo.count;
            if (pinfo.count == 0 || seasons.size() == before || (pinfo.totalItems >= 0 && done >= pinfo.totalItems)
                || (pinfo.perPage > 0 && pinfo.count < pinfo.perPage)) {
                break;
            }
        }
        for (const auto &se: seasons) {
            if (token.canceled()) {
                return;
            }
            Season season;
            season.number = se.number;
            season.name = se.name;
            if (!se.episodes.empty() && !se.cmd.empty()) {
                for (int n: se.episodes) {
                    Episode ep;
                    ep.id = seriesId + ":" + std::to_string(se.number) + ":" + std::to_string(n);
                    ep.season = se.number;
                    ep.number = n;
                    ep.cmd = se.cmd;
                    ep.linkSeries = n;
                    season.episodes.push_back(ep);
                }
            } else {
                for (int page = 1; page <= SEASON_MAX_PAGES; page++) {
                    Call c = request(*s, "series", "get_ordered_list",
                                     "&movie_id=" + url::encode(seriesId) + "&season_id=" + url::encode(se.id)
                                     + "&episode_id=0&p=" + std::to_string(page), token);
                    stalker::Page pinfo;
                    size_t before = season.episodes.size();
                    if (c.fail != Fail::None || !stalker::parseEpisodes(c.response.body, s->portal.origin, se.number,
                                                                        season.episodes, pinfo, err)) {
                        break;
                    }
                    int done = (page - 1) * std::max(1, pinfo.perPage) + pinfo.count;
                    if (pinfo.count == 0 || season.episodes.size() == before
                        || (pinfo.totalItems >= 0 && done >= pinfo.totalItems)
                        || (pinfo.perPage > 0 && pinfo.count < pinfo.perPage)) {
                        break;
                    }
                }
            }
            std::sort(season.episodes.begin(), season.episodes.end(),
                      [](const Episode &a, const Episode &b) { return a.number < b.number; });
            if (!season.episodes.empty()) {
                info.seasons.push_back(std::move(season));
            }
        }
        std::sort(info.seasons.begin(), info.seasons.end(),
                  [](const Season &a, const Season &b) { return a.number < b.number; });
        if (info.seasons.empty()) {
            out->message = i18n::tr("stalker.no_episodes");
            return;
        }
        out->info = std::move(info);
        out->ok = true;
        LOG_I("stalker", "series %s: %d seasons", seriesId.c_str(), (int) out->info.seasons.size());
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StalkerService::resolveLive(const Profile &profile, const LiveChannel &channel,
                                        sources::LinkCallback callback) {
    auto out = std::make_shared<sources::LinkOutcome>();
    std::shared_ptr<Session> s = sessionFor(profile);
    std::string cmd = channel.cmd;
    return jobs.submit(JobPriority::High, "stalker link", [s, out, cmd](const CancelToken &token) {
        *out = createLink(*s, "itv", cmd, 0, token);
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StalkerService::resolveMovie(const Profile &profile, const std::string &movieId,
                                         sources::LinkCallback callback) {
    auto out = std::make_shared<sources::LinkOutcome>();
    std::shared_ptr<Session> s = sessionFor(profile);
    std::shared_ptr<Store> st = store;
    std::string profileId = profile.id;
    std::string dataDir = dir;
    return jobs.submit(JobPriority::High, "stalker link", [=](const CancelToken &token) {
        stalker::VodEntry e;
        if (!lookup(*st, profileId, true, movieId, e)) {
            rememberFromCache(*st, dataDir, profileId, true);
        }
        if (!lookup(*st, profileId, true, movieId, e) || e.cmd.empty()) {
            out->message = i18n::tr("stalker.link.unknown");
            LOG_W("stalker", "movie %s: no play command known (open Movies once to load the list)", movieId.c_str());
            return;
        }
        *out = createLink(*s, "vod", e.cmd, 0, token);
    }, [out, callback]() {
        callback(*out);
    });
}

CancelToken StalkerService::resolveEpisode(const Profile &profile, const std::string &cmd, int series,
                                           sources::LinkCallback callback) {
    auto out = std::make_shared<sources::LinkOutcome>();
    std::shared_ptr<Session> s = sessionFor(profile);
    return jobs.submit(JobPriority::High, "stalker link", [s, out, cmd, series](const CancelToken &token) {
        if (cmd.empty()) {
            out->message = i18n::tr("stalker.link.unknown");
            return;
        }
        *out = createLink(*s, "vod", cmd, series, token);
    }, [out, callback]() {
        callback(*out);
    });
}
