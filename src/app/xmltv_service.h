// XMLTV programme guides of playlist (M3U) sources on the job system (iptv/xmltv.h). Callbacks run on the main
// thread.
//
// The live player's guide panel asks for one channel at a time (SourceService::loadGuide). The first time a
// playlist's guide is needed, its saved guide is opened (<data>/cache/<profile>/xmltv_guide.bin). When there is none,
// it was built from other addresses, or it no longer covers the next hours, the XMLTV feed(s) the playlist header
// names are downloaded ONCE on a worker:
//   - streamed, plain or gzip, through the XMLTV reader into a compact saved guide - the feed itself is never held
//     in memory or written to disk;
//   - at most MAX_SPEED, so the stream keeps its bandwidth; at most MAX_DOWNLOAD bytes as sent and MAX_XML once
//     decompressed;
//   - only the channels the playlist has, only the next hours (xmltv::BuildLimits).
// A saved guide older than REFRESH_AFTER is rebuilt the same way in the background while it keeps answering. After
// a failed download nothing is tried again for RETRY_AFTER (remembered across launches). A request that arrives
// meanwhile waits (a few, one per channel) and is answered when the guide is ready. Nothing is fetched for a
// playlist without a guide address, and nothing in the background otherwise.
//
// Guide addresses can carry credentials: only m3u::displayUrl() (host and file name) is ever logged.

#ifndef PS4IPTV_APP_XMLTV_SERVICE_H
#define PS4IPTV_APP_XMLTV_SERVICE_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source_types.h"
#include "../iptv/catalog.h"
#include "../iptv/models.h"
#include "../iptv/xmltv.h"
#include "../network/jobs.h"

class XmltvService {

public:

    static constexpr int64_t REFRESH_AFTER = 12 * 3600;   // a saved guide is rebuilt (in the background) after
    static constexpr int64_t RETRY_AFTER = 3600;          // a failed download is not tried again for
    static constexpr int64_t MIN_COVERAGE = 3 * 3600;     // an older guide still answers while it covers this much
    static constexpr long MAX_SPEED = 1024 * 1024;        // bytes per second
    static constexpr size_t MAX_DOWNLOAD = 160u << 20;    // bytes as sent (gzip or plain)
    static constexpr size_t MAX_XML = 1024u << 20;        // bytes of XML read (after gzip)
    static constexpr size_t MAX_WAITING = 8;              // requests waiting for a guide
    static constexpr const char *FILE_NAME = "xmltv_guide.bin";
    static constexpr const char *FAILED_NAME = "xmltv_guide.failed";

    XmltvService(JobSystem &jobs, std::string dataDir);

    // a new sign-in: the loaded guide and the waiting requests are forgotten, a running download stops
    void reset();

    // one channel's programmes still running or to come (ok + an empty list: the guide has nothing for it).
    // offline: the saved guide only, no download.
    void load(const iptv::Profile &profile, const iptv::LiveCatalog &live, const iptv::LiveChannel &channel,
              bool offline, sources::GuideCallback callback);

    // the saved guide's location of a profile (Settings > clear cache / a deleted profile remove the folder)
    std::string guidePath(const std::string &profileId) const;

private:

    // what a download builds the guide from: copied from the catalog when it starts (the catalog can change)
    struct Plan {
        std::string profileId;
        uint64_t key = 0;
        std::vector<std::string> urls;
        std::string userAgent;
        std::string language;
        std::vector<std::pair<std::string, std::string>> channels;   // tvg-id, name
    };

    struct Waiting {
        iptv::LiveChannel channel;
        sources::GuideCallback callback;
    };

    enum class Work {
        None,
        Opening,    // reading the saved guide's table
        Building    // downloading and building a guide
    };

    std::shared_ptr<Plan> plan(const iptv::Profile &p, const iptv::LiveCatalog &live) const;

    void open(std::shared_ptr<Plan> plan, bool offline);

    void build(std::shared_ptr<Plan> plan);

    // after the saved guide was opened / a download ended: answer the waiting requests, or start a download
    void resume(const std::shared_ptr<Plan> &plan, bool offline);

    void answer(const iptv::LiveChannel &channel, sources::GuideCallback callback);

    void wait(const iptv::LiveChannel &channel, sources::GuideCallback callback);

    void answerWaiting();

    void failWaiting();

    bool fresh(int64_t now) const;

    bool usable(int64_t now) const;

    bool failedRecently(int64_t now) const;

    JobSystem &jobs;
    std::string dataDir;
    std::string profileId;
    uint64_t key = 0;
    std::shared_ptr<const xmltv::Index> index;
    Work work = Work::None;
    CancelToken workToken;
    bool opened = false;     // the saved guide of profileId / key was looked at
    int64_t failedAt = 0;    // the last failed download (unix time)
    std::vector<Waiting> waiting;
};

#endif // PS4IPTV_APP_XMLTV_SERVICE_H
