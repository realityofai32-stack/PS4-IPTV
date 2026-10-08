// Offline playback of downloads (host-testable glue between the download manager and the VOD player).
//
// A download plays through the same player and mpv backend as a stream - only the source differs (a local
// path instead of the provider URL). Progress, Continue Watching and watched state are keyed by content type
// + id in the download's profile, never by the source, so a movie started streaming resumes offline at the
// same position and the other way round.

#ifndef PS4IPTV_APP_OFFLINE_H
#define PS4IPTV_APP_OFFLINE_H

#include <string>
#include <vector>

#include "screens.h"
#include "../downloads/download_manager.h"

namespace offline {

    dl::Kind kindOf(iptv::ContentType type);

    // the completed download's file of a movie / episode (it exists on the disk), "" when there is none
    std::string localFile(const dl::DownloadManager &downloads, const std::string &profileId, iptv::ContentType type,
                          const std::string &id);

    // everything the player and the progress store need, from the manifest alone (no provider, no network)
    screens::VodItem itemFor(const dl::DownloadManager &downloads, const dl::Item &download);

    // completed episodes of a series in a profile, in season / episode order: the offline "next episode" queue
    std::vector<dl::Item> seriesEpisodes(const dl::DownloadManager &downloads, const std::string &profileId,
                                         const std::string &seriesId);

    // when the same movie / episode was downloaded, the item plays the local file (else it is unchanged)
    void preferLocal(const dl::DownloadManager &downloads, screens::VodItem &item, const std::string &profileId);
}

#endif // PS4IPTV_APP_OFFLINE_H
