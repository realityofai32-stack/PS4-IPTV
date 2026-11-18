#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include "user_groups.h"

namespace usergroups {

    namespace {
        uint64_t fnv(const std::string &s) {
            uint64_t h = 1469598103934665603ull;
            for (unsigned char c: s) {
                h ^= c;
                h *= 1099511628211ull;
            }
            return h;
        }

        // from the address alone: the same stream keeps its id whichever playlist lists it first
        std::string extraId(const userpl::Entry &e, std::unordered_set<std::string> &used) {
            char buf[24];
            snprintf(buf, sizeof(buf), "u%016llx", (unsigned long long) fnv(e.url));
            std::string id = buf;
            for (int n = 2; !used.insert(id).second; n++) {
                id = std::string(buf) + "-" + std::to_string(n);   // (two addresses with the same hash)
            }
            return id;
        }
    }

    std::string groupId(const std::string &file) {
        return "userpl:" + file;
    }

    Overlay build(const iptv::LiveCatalog &live, const std::string &profileId, const std::vector<Playlist> &playlists) {
        Overlay o;
        const auto &channels = live.channels();
        const int provider = (int) live.providerCount();
        std::unordered_map<std::string, int> byUrl;   // a playlist source's channels by address (built when needed)
        bool urlsBuilt = false;
        std::unordered_set<std::string> usedIds;
        std::unordered_map<std::string, int> extraByUrl;   // one extra channel per address, whichever playlists list it
        for (const auto &pl: playlists) {
            iptv::LiveCatalog::UserGroup g;
            g.category.id = groupId(pl.file);
            g.category.name = pl.title.empty() ? pl.file : pl.title;
            std::unordered_set<int> inGroup;
            for (const auto &e: pl.entries) {
                int index = -1;
                if (!e.sourceId.empty() && e.sourceId == profileId && !e.channelId.empty()) {
                    const iptv::LiveChannel *c = live.find(e.channelId);
                    int at = c != nullptr ? (int) (c - channels.data()) : -1;
                    index = at >= 0 && at < provider ? at : -1;
                }
                if (index < 0 && !e.url.empty()) {
                    if (!urlsBuilt) {
                        for (int i = 0; i < provider; i++) {
                            if (!channels[(size_t) i].url.empty()) {
                                byUrl.emplace(channels[(size_t) i].url, i);
                            }
                        }
                        urlsBuilt = true;
                    }
                    auto it = byUrl.find(e.url);
                    if (it != byUrl.end()) {
                        index = it->second;
                    }
                }
                if (index >= 0) {
                    o.referenced++;
                } else if (e.url.empty()) {
                    o.skipped++;
                    continue;
                } else if (extraByUrl.count(e.url) > 0) {
                    index = extraByUrl[e.url];   // already an extra channel (this playlist or an earlier one)
                } else {
                    iptv::LiveChannel c;
                    c.id = extraId(e, usedIds);
                    c.name = e.name;
                    c.icon = e.logo;
                    c.url = e.url;
                    c.userAgent = e.userAgent;
                    c.epgId = e.tvgId;
                    c.categoryId = g.category.id;
                    index = provider + (int) o.extra.size();
                    extraByUrl.emplace(e.url, index);
                    o.extra.push_back(std::move(c));
                    o.added++;
                }
                if (inGroup.insert(index).second) {
                    g.channels.push_back(index);
                }
            }
            o.groups.push_back(std::move(g));
        }
        return o;
    }

    std::vector<userpl::Summary> ordered(const std::vector<std::string> &order, std::vector<userpl::Summary> onDisk) {
        std::vector<userpl::Summary> out;
        out.reserve(onDisk.size());
        for (const auto &f: order) {
            auto it = std::find_if(onDisk.begin(), onDisk.end(), [&f](const userpl::Summary &s) { return s.file == f; });
            if (it != onDisk.end()) {
                out.push_back(std::move(*it));
                onDisk.erase(it);
            }
        }
        for (auto &s: onDisk) {
            out.push_back(std::move(s));   // not ordered yet (new, or copied over FTP): after, by title (list() order)
        }
        return out;
    }

    std::vector<std::string> shownFiles(const std::vector<std::string> &order, const std::set<std::string> &hidden,
                                        const std::vector<userpl::Summary> &onDisk) {
        std::vector<std::string> out;
        for (const auto &s: ordered(order, onDisk)) {
            if (s.editable && !hidden.count(s.file)) {
                out.push_back(s.file);
            }
        }
        return out;
    }
}
