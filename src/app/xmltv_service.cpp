#include <cstdio>

#include "xmltv_service.h"
#include "../i18n/i18n.h"
#include "../iptv/m3u.h"
#include "../network/gunzip.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/fs.h"
#include "../platform/log.h"

namespace {
    struct OpenResult {
        std::shared_ptr<const xmltv::Index> index;
        int64_t failedAt = 0;
    };

    struct BuildResult {
        std::shared_ptr<const xmltv::Index> index;
        bool failed = false;
    };

    struct ReadResult {
        sources::GuideOutcome outcome;
        bool gone = false;   // the saved guide was removed or replaced since its Index was opened
    };

    std::string folderOf(const std::string &dataDir, const std::string &profileId) {
        return fs::join(fs::join(dataDir, "cache"), profileId);
    }

    // "<unix time> <source key>": the last failed download of these addresses
    int64_t readFailure(const std::string &folder, uint64_t key) {
        std::string text;
        if (!fs::readFile(fs::join(folder, XmltvService::FAILED_NAME), text, 256)) {
            return 0;
        }
        long long at = 0;
        unsigned long long k = 0;
        return sscanf(text.c_str(), "%lld %llu", &at, &k) == 2 && k == key ? (int64_t) at : 0;
    }

    void writeFailure(const std::string &folder, uint64_t key, int64_t at) {
        if (fs::ensureDir(folder)) {
            fs::writeFileReplace(fs::join(folder, XmltvService::FAILED_NAME),
                                 std::to_string(at) + " " + std::to_string(key) + "\n");
        }
    }
}

XmltvService::XmltvService(JobSystem &j, std::string dir) : jobs(j), dataDir(std::move(dir)) {}

std::string XmltvService::guidePath(const std::string &id) const {
    return fs::join(folderOf(dataDir, id), FILE_NAME);
}

void XmltvService::reset() {
    workToken.cancel();
    workToken = CancelToken();
    work = Work::None;
    index.reset();
    opened = false;
    failedAt = 0;
    profileId.clear();
    key = 0;
    waiting.clear();
}

bool XmltvService::fresh(int64_t now) const {
    return index && now - index->savedAt < REFRESH_AFTER && index->savedAt - now < 3600;   // (a clock set back)
}

bool XmltvService::usable(int64_t now) const {
    return index && (fresh(now) || index->coverageEnd > now + MIN_COVERAGE);
}

bool XmltvService::failedRecently(int64_t now) const {
    return failedAt > 0 && now >= failedAt && now - failedAt < RETRY_AFTER;
}

std::shared_ptr<XmltvService::Plan> XmltvService::plan(const iptv::Profile &p, const iptv::LiveCatalog &live) const {
    auto plan = std::make_shared<Plan>();
    plan->profileId = p.id;
    plan->key = xmltv::sourceKey(live.guideUrls);
    plan->urls = live.guideUrls;
    plan->userAgent = p.userAgent;
    plan->language = i18n::code(i18n::language());
    plan->channels.reserve(live.channels().size());
    for (const auto &c: live.channels()) {
        plan->channels.emplace_back(c.epgId, c.name);
    }
    return plan;
}

void XmltvService::load(const iptv::Profile &p, const iptv::LiveCatalog &live, const iptv::LiveChannel &channel,
                        bool offline, sources::GuideCallback callback) {
    uint64_t k = xmltv::sourceKey(live.guideUrls);
    if (p.id != profileId || k != key) {
        reset();   // another source, or the playlist names other guide addresses
        profileId = p.id;
        key = k;
    }
    if (live.guideUrls.empty()) {
        sources::GuideOutcome o;
        o.ok = true;   // no guide: nothing to show, nothing to fetch
        callback(o);
        return;
    }
    int64_t now = clockx::unixNow();
    if (usable(now)) {
        if (!fresh(now) && work == Work::None && !offline && !failedRecently(now)) {
            build(plan(p, live));   // in the background; this guide answers meanwhile
        }
        answer(channel, std::move(callback));
        return;
    }
    wait(channel, std::move(callback));
    if (work != Work::None) {
        return;
    }
    if (!opened) {
        open(plan(p, live), offline);
    } else if (offline || failedRecently(now)) {
        failWaiting();
    } else {
        build(plan(p, live));
    }
}

void XmltvService::open(std::shared_ptr<Plan> pl, bool offline) {
    work = Work::Opening;
    auto result = std::make_shared<OpenResult>();
    std::string folder = folderOf(dataDir, pl->profileId);
    workToken = jobs.submit(JobPriority::High, "xmltv (saved)", [result, folder, pl](const CancelToken &) {
        auto idx = std::make_shared<xmltv::Index>();
        std::string err;
        std::string file = fs::join(folder, FILE_NAME);
        if (idx->open(file, err)) {
            if (idx->sourceKey == pl->key) {
                result->index = idx;
                LOG_I("xmltv", "saved guide: %zu channels, saved %lld s ago, covers %lld s more", idx->size(),
                      (long long) (clockx::unixNow() - idx->savedAt), (long long) (idx->coverageEnd - clockx::unixNow()));
            } else {
                LOG_I("xmltv", "saved guide built from other addresses: not used");
            }
        } else if (fs::exists(file)) {
            LOG_W("xmltv", "saved guide not used: %s", err.c_str());
        }
        result->failedAt = readFailure(folder, pl->key);
    }, [this, result, pl, offline]() {
        work = Work::None;
        opened = true;
        if (pl->profileId != profileId || pl->key != key) {
            return;
        }
        index = result->index;
        failedAt = std::max(failedAt, result->failedAt);
        resume(pl, offline);
    });
}

void XmltvService::resume(const std::shared_ptr<Plan> &pl, bool offline) {
    int64_t now = clockx::unixNow();
    bool canDownload = !offline && !failedRecently(now);
    if (usable(now)) {
        if (!fresh(now) && canDownload) {
            build(pl);
        }
        answerWaiting();
    } else if (canDownload) {
        build(pl);
    } else if (index) {
        answerWaiting();   // an old guide: what it still has (ended programmes are never shown)
    } else {
        failWaiting();
    }
}

void XmltvService::build(std::shared_ptr<Plan> pl) {
    work = Work::Building;
    auto result = std::make_shared<BuildResult>();
    std::string folder = folderOf(dataDir, pl->profileId);
    workToken = jobs.submit(JobPriority::Normal, "xmltv", [result, folder, pl](const CancelToken &token) {
        int64_t started = clockx::unixNow();
        double t0 = clockx::monotonic();
        xmltv::Wanted wanted;
        for (const auto &c: pl->channels) {
            wanted.add(c.first, c.second);
        }
        xmltv::Builder builder(std::move(wanted), started);
        int usableFeeds = 0;
        size_t sent = 0;
        for (const auto &url: pl->urls) {
            std::string where = m3u::displayUrl(url);
            xmltv::Reader reader(builder, pl->language);
            std::unique_ptr<gz::Inflater> inflater;
            int kind = 0;   // 0: not known yet, 1: plain XML, 2: gzip
            std::string head;
            bool notXml = false;
            bool tooLarge = false;
            auto toReader = [&](const char *d, size_t n) {
                if (reader.stats().bytes + n > MAX_XML) {
                    tooLarge = true;
                    return false;
                }
                if (!reader.feed(d, n)) {
                    notXml = true;
                    return false;
                }
                return !token.canceled();
            };
            http::Request req;
            req.url = url;
            req.cancel = token.shared();
            req.connectTimeoutMs = 10000;
            req.totalTimeoutMs = 20 * 60 * 1000;   // MAX_DOWNLOAD at MAX_SPEED is under three minutes
            req.stallTimeoutS = 30;
            req.maxBytes = MAX_DOWNLOAD;
            req.userAgent = pl->userAgent;
            req.maxBytesPerSecond = MAX_SPEED;
            req.sink = [&](const char *d, size_t n) {
                if (kind == 0) {
                    head.append(d, n);
                    if (head.size() < 2) {
                        return true;
                    }
                    kind = gz::isGzip(head.data(), head.size()) ? 2 : 1;
                    if (kind == 2) {
                        inflater.reset(new gz::Inflater(MAX_XML));
                        return inflater->feed(head.data(), head.size(), toReader);
                    }
                    return toReader(head.data(), head.size());
                }
                return kind == 2 ? inflater->feed(d, n, toReader) : toReader(d, n);
            };
            http::Response resp = http::get(req);
            if (token.canceled()) {
                return;
            }
            if (kind == 0 && !head.empty()) {
                toReader(head.data(), head.size());
            }
            bool xml = reader.finish() && !notXml;
            sent += resp.streamed;
            const xmltv::ReaderStats &rs = reader.stats();
            if (!resp.ok() && !(resp.error == http::Error::Stopped && xml && !tooLarge)) {
                std::string why = resp.error == http::Error::Stopped
                                  ? (tooLarge ? "too large" : notXml ? "not an XMLTV guide"
                                                                     : inflater ? inflater->error() : "stopped")
                                  : http::describe(resp) + (resp.detail.empty() ? "" : " (" + resp.detail + ")");
                LOG_W("xmltv", "%s: %s after %zu KiB in %.1fs", where.c_str(), why.c_str(), resp.streamed / 1024,
                      resp.seconds);
                continue;
            }
            if (!xml) {
                LOG_W("xmltv", "%s: not an XMLTV guide (%zu KiB)", where.c_str(), resp.streamed / 1024);
                continue;
            }
            bool truncated = kind == 2 && !inflater->complete();
            LOG_I("xmltv", "%s: %zu KiB%s -> %zu KiB XML in %.1fs%s: %d channels, %d programmes (%d without a usable "
                           "start, %d without a title, %d oversized parts)", where.c_str(), resp.streamed / 1024,
                  kind == 2 ? " gzip" : "", rs.bytes / 1024, resp.seconds, truncated ? " (compressed data cut short)" : "",
                  rs.channels, rs.programmes, rs.badTimes, rs.noTitle, rs.oversized);
            usableFeeds++;
        }
        if (usableFeeds == 0) {
            result->failed = true;
            writeFailure(folder, pl->key, clockx::unixNow());
            return;
        }
        std::string err;
        std::string file = fs::join(folder, FILE_NAME);
        auto idx = std::make_shared<xmltv::Index>();
        if (!fs::ensureDir(folder, &err) || !builder.write(file, pl->key, err) || !idx->open(file, err)) {
            LOG_W("xmltv", "guide not saved: %s", err.c_str());
            result->failed = true;
            writeFailure(folder, pl->key, clockx::unixNow());
            return;
        }
        fs::removeFile(fs::join(folder, FAILED_NAME));
        const xmltv::BuildStats &bs = builder.stats();
        LOG_I("xmltv", "guide saved: %d guide channels matched (playlist: %zu channels), %d with programmes, %d "
                       "programmes kept (%d not in the playlist, %d outside the next hours, %d dropped, %d over the "
                       "memory limit, %d descriptions dropped), %zu KiB read in %.1fs",
              bs.matchedChannels, pl->channels.size(), bs.channelsWritten, bs.kept, bs.unmatchedProgrammes,
              bs.outsideWindow, bs.dropped, bs.overLimit, bs.descriptionsDropped, sent / 1024,
              clockx::monotonic() - t0);
        result->index = idx;
    }, [this, result, pl]() {
        work = Work::None;
        if (pl->profileId != profileId || pl->key != key) {
            return;
        }
        int64_t now = clockx::unixNow();
        if (result->index) {
            index = result->index;
            failedAt = 0;
        } else {
            failedAt = now;
        }
        if (index) {
            answerWaiting();   // the new guide, or the old one after a failed refresh
        } else {
            failWaiting();
        }
    });
}

void XmltvService::answer(const iptv::LiveChannel &channel, sources::GuideCallback callback) {
    std::shared_ptr<const xmltv::Index> idx = index;
    int slot = idx ? idx->find(channel) : -1;
    if (slot < 0) {
        sources::GuideOutcome o;
        o.ok = idx != nullptr;   // the guide has nothing for this channel: "no program information"
        if (!o.ok) {
            o.message = i18n::tr("live.program_failed");
        }
        callback(o);
        return;
    }
    auto result = std::make_shared<ReadResult>();
    jobs.submit(JobPriority::High, "xmltv channel", [idx, slot, result](const CancelToken &) {
        std::string err;
        result->outcome.ok = idx->read(slot, clockx::unixNow(), result->outcome.programmes, err, &result->gone);
        if (!result->outcome.ok) {
            result->outcome.message = i18n::tr("live.program_failed");
            if (!result->gone) {
                LOG_W("xmltv", "saved guide: %s", err.c_str());
            }
        }
    }, [this, idx, result, channel, callback]() {
        if (result->gone) {
            if (index && index != idx) {
                answer(channel, callback);   // a newer guide is loaded meanwhile: it answers
                return;
            }
            if (index == idx && work == Work::Building) {
                wait(channel, callback);     // the new guide is being finished: answered with it
                return;
            }
            if (index == idx) {
                index.reset();               // removed (Settings > clear cache): looked for again next time
                opened = false;
            }
        }
        callback(result->outcome);
    });
}

void XmltvService::wait(const iptv::LiveChannel &channel, sources::GuideCallback callback) {
    for (auto &w: waiting) {
        if (w.channel.id == channel.id) {
            w.callback = std::move(callback);
            return;
        }
    }
    if (waiting.size() >= MAX_WAITING) {
        sources::GuideOutcome o;
        o.message = i18n::tr("live.program_failed");
        sources::GuideCallback oldest = std::move(waiting.front().callback);
        waiting.erase(waiting.begin());
        oldest(o);   // asked again later by its panel
    }
    waiting.push_back({channel, std::move(callback)});
}

void XmltvService::answerWaiting() {
    std::vector<Waiting> now;
    now.swap(waiting);
    for (auto &w: now) {
        answer(w.channel, std::move(w.callback));
    }
}

void XmltvService::failWaiting() {
    std::vector<Waiting> now;
    now.swap(waiting);
    for (auto &w: now) {
        sources::GuideOutcome o;
        o.message = i18n::tr("live.program_failed");
        w.callback(o);
    }
}
