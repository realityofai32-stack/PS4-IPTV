#include <algorithm>

#include "offline.h"
#include "../platform/fs.h"

namespace offline {

    dl::Kind kindOf(iptv::ContentType type) {
        return type == iptv::ContentType::Series ? dl::Kind::Episode : dl::Kind::Movie;
    }

    std::string localFile(const dl::DownloadManager &downloads, const std::string &profileId, iptv::ContentType type,
                          const std::string &id) {
        dl::Item d;
        if (!downloads.findCompleted(profileId, kindOf(type), id, d)) {
            return "";
        }
        std::string path = downloads.completedPath(d);
        return fs::fileSize(path) > 0 ? path : "";
    }

    screens::VodItem itemFor(const dl::DownloadManager &downloads, const dl::Item &d) {
        screens::VodItem it;
        it.type = d.kind == dl::Kind::Episode ? iptv::ContentType::Series : iptv::ContentType::Movie;
        it.id = d.contentId;
        it.extension = d.extension;
        it.title = d.title;
        it.image = d.poster;
        it.year = d.year;
        it.seriesId = d.seriesId;
        it.seriesName = d.seriesName;
        it.season = d.season;
        it.episode = d.episode;
        it.durationHint = d.durationHint;
        it.profileId = d.profileId;
        it.localPath = downloads.completedPath(d);
        return it;
    }

    std::vector<dl::Item> seriesEpisodes(const dl::DownloadManager &downloads, const std::string &profileId,
                                         const std::string &seriesId) {
        std::vector<dl::Item> out;
        for (const dl::Item &d: downloads.items()) {
            if (d.kind == dl::Kind::Episode && d.state == dl::State::Completed && d.profileId == profileId
                && d.seriesId == seriesId) {
                out.push_back(d);
            }
        }
        std::sort(out.begin(), out.end(), [](const dl::Item &a, const dl::Item &b) {
            return a.season != b.season ? a.season < b.season : a.episode != b.episode ? a.episode < b.episode
                                                                                         : a.order < b.order;
        });
        return out;
    }

    void preferLocal(const dl::DownloadManager &downloads, screens::VodItem &item, const std::string &profileId) {
        std::string path = localFile(downloads, profileId, item.type, item.id);
        if (!path.empty()) {
            item.localPath = path;
            item.profileId = profileId;
        }
    }
}
