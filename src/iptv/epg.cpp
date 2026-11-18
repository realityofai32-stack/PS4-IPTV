#include <algorithm>

#include "epg.h"
#include "stalker.h"
#include "xtream.h"
#include "../core/json.h"
#include "../core/url.h"

namespace epg {

    namespace {
        const int64_t YEAR_2100 = 4102444800LL;

        int base64Value(char c) {
            if (c >= 'A' && c <= 'Z') {
                return c - 'A';
            }
            if (c >= 'a' && c <= 'z') {
                return c - 'a' + 26;
            }
            if (c >= '0' && c <= '9') {
                return c - '0' + 52;
            }
            return c == '+' ? 62 : c == '/' ? 63 : -1;
        }

        // standard alphabet, padded form only (length a multiple of 4, '=' only at the end)
        bool base64(const std::string &in, std::string &out) {
            if (in.empty() || in.size() % 4 != 0) {
                return false;
            }
            size_t pad = 0;
            while (pad < 2 && in[in.size() - 1 - pad] == '=') {
                pad++;
            }
            out.clear();
            out.reserve(in.size() / 4 * 3);
            uint32_t acc = 0;
            int bits = 0;
            for (size_t i = 0; i < in.size() - pad; i++) {
                int v = base64Value(in[i]);
                if (v < 0) {
                    return false;
                }
                acc = (acc << 6) | (uint32_t) v;
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    out += (char) ((acc >> bits) & 0xFF);
                }
            }
            return true;
        }

        // strict UTF-8 (no overlong forms, no surrogates, at most U+10FFFF)
        bool validUtf8(const std::string &s) {
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = (unsigned char) s[i];
                if (c < 0x80) {
                    i++;
                    continue;
                }
                size_t len;
                uint32_t cp;
                if (c >= 0xC2 && c <= 0xDF) {
                    len = 2;
                    cp = c & 0x1F;
                } else if ((c & 0xF0) == 0xE0) {
                    len = 3;
                    cp = c & 0x0F;
                } else if (c >= 0xF0 && c <= 0xF4) {
                    len = 4;
                    cp = c & 0x07;
                } else {
                    return false;
                }
                if (i + len > s.size()) {
                    return false;
                }
                for (size_t k = 1; k < len; k++) {
                    unsigned char cc = (unsigned char) s[i + k];
                    if ((cc & 0xC0) != 0x80) {
                        return false;
                    }
                    cp = (cp << 6) | (cc & 0x3F);
                }
                if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF
                    || (cp >= 0xD800 && cp <= 0xDFFF)) {
                    return false;
                }
                i += len;
            }
            return true;
        }

        bool hasControl(const std::string &s) {
            for (unsigned char c: s) {
                if ((c < 0x20 && c != '\t' && c != '\n' && c != '\r') || c == 0x7F) {
                    return true;
                }
            }
            return false;
        }

        // one line of text: control characters / newlines / runs of spaces become one space, provider placeholders
        // ("-", "N/A", "null") become "", at most `cap` bytes (cut at a character boundary, an ellipsis added)
        std::string cleanField(const std::string &raw, size_t cap) {
            std::string out;
            bool space = false;
            for (unsigned char c: raw) {
                if (c <= 0x20 || c == 0x7F) {
                    space = true;
                    continue;
                }
                if (space && !out.empty()) {
                    out += ' ';
                }
                space = false;
                out += (char) c;
            }
            out = xtream::cleanText(out);
            if (out.size() > cap) {
                size_t cut = cap;
                while (cut > 0 && ((unsigned char) out[cut] & 0xC0) == 0x80) {
                    cut--;   // never inside a character
                }
                out.resize(cut);
                while (!out.empty() && out.back() == ' ') {
                    out.pop_back();
                }
                out += "\xE2\x80\xA6";   // ...
            }
            return out;
        }

        void add(std::vector<Programme> &out, int64_t start, int64_t stop, const std::string &title,
                 const std::string &description) {
            if (start <= 0 || stop <= start || stop - start > MAX_DURATION || start >= YEAR_2100) {
                return;   // no usable time: never shown with a guessed one
            }
            Programme p;
            p.start = start;
            p.stop = stop;
            p.title = cleanField(title, MAX_TITLE);
            if (p.title.empty()) {
                return;
            }
            p.description = cleanField(description, MAX_DESCRIPTION);
            out.push_back(std::move(p));
        }

        // sorted by start; the same programme twice (start within a minute) kept once; a programme that runs into
        // the next one ends where the next one starts (the provider's own times); at most MAX_PROGRAMMES
        void finish(std::vector<Programme> &out) {
            std::stable_sort(out.begin(), out.end(), [](const Programme &a, const Programme &b) {
                return a.start < b.start;
            });
            std::vector<Programme> kept;
            for (auto &p: out) {
                if (!kept.empty()) {
                    Programme &last = kept.back();
                    if (p.start < last.start + 60) {
                        continue;
                    }
                    if (p.start < last.stop) {
                        last.stop = p.start;
                    }
                }
                kept.push_back(std::move(p));
                if (kept.size() >= MAX_PROGRAMMES) {
                    break;
                }
            }
            out = std::move(kept);
        }

        const json::Value *listOf(const json::Value &v, std::vector<json::Value> &members) {
            if (v.isArray()) {
                return &v;
            }
            if (v.isObject()) {
                // a few panels send the list as an object keyed "0", "1", ...
                members.clear();
                for (const auto &m: v.members()) {
                    members.push_back(m.second);
                }
                return nullptr;
            }
            return nullptr;
        }
    }

    Source sourceFor(iptv::SourceType type, const iptv::LiveChannel &channel, bool playlistGuide) {
        if (channel.id.empty()) {
            return Source::None;
        }
        switch (type) {
            case iptv::SourceType::Xtream:
                // (a channel with its own address here is a user playlist's entry: not the provider's)
                return channel.epgId.empty() || channel.isPlaylist() ? Source::None : Source::Xtream;
            case iptv::SourceType::Stalker:
                return channel.isPlaylist() ? Source::None : Source::Stalker;   // the portal's guide by channel id
            case iptv::SourceType::M3u:
                // playlists: only with the XMLTV guide their header names (matched by tvg-id, else by name)
                return playlistGuide ? Source::Xmltv : Source::None;
            default:
                return Source::None;
        }
    }

    std::string cleanLine(const std::string &raw, size_t cap) {
        return cleanField(raw, cap);
    }

    std::string decodeText(const std::string &s) {
        std::string t = url::trim(s);
        std::string decoded;
        if (base64(t, decoded) && !decoded.empty() && validUtf8(decoded) && !hasControl(decoded)) {
            return decoded;
        }
        return s;
    }

    bool parseXtream(const std::string &body, std::vector<Programme> &out, std::string &error) {
        out.clear();
        json::Value root;
        std::string perr;
        if (!json::parse(body, root, &perr)) {
            error = "not JSON (" + perr + ")";
            return false;
        }
        const json::Value *list = nullptr;
        std::vector<json::Value> members;
        if (root.isArray()) {
            list = &root;
        } else if (root.isObject() && root.has("epg_listings")) {
            list = listOf(root["epg_listings"], members);   // null / false: the panel has nothing for the stream
        } else {
            error = "no epg_listings";
            return false;
        }
        const std::vector<json::Value> &items = list ? list->items() : members;
        for (const auto &item: items) {
            if (item.isObject()) {
                add(out, item["start_timestamp"].asInt(0), item["stop_timestamp"].asInt(0),
                    decodeText(item["title"].asString()), decodeText(item["description"].asString()));
            }
        }
        finish(out);
        return true;
    }

    bool parseStalker(const std::string &body, std::vector<Programme> &out, std::string &error) {
        out.clear();
        json::Value js;
        stalker::Reply r = stalker::envelope(body, js);
        if (r != stalker::Reply::Ok) {
            error = r == stalker::Reply::AuthFailed ? "authorization failed" : "not a portal answer";
            return false;
        }
        std::vector<json::Value> members;
        const json::Value *list = js.isObject() && js.has("data") ? listOf(js["data"], members) : listOf(js, members);
        const std::vector<json::Value> &items = list ? list->items() : members;
        for (const auto &item: items) {
            if (item.isObject()) {
                add(out, item["start_timestamp"].asInt(0), item["stop_timestamp"].asInt(0), item["name"].asString(),
                    item["descr"].asString());
            }
        }
        finish(out);
        return true;
    }

    NowNext nowNext(const std::vector<Programme> &list, int64_t now) {
        NowNext r;
        for (int i = 0; i < (int) list.size(); i++) {
            const Programme &p = list[(size_t) i];
            if (p.start <= now && now < p.stop) {
                r.now = i;
            } else if (p.start > now) {
                r.next = i;
                break;
            }
        }
        return r;
    }

    double progress(const Programme &p, int64_t now) {
        if (p.stop <= p.start || now <= p.start) {
            return 0;
        }
        if (now >= p.stop) {
            return 1;
        }
        return (double) (now - p.start) / (double) (p.stop - p.start);
    }

    int minutesLeft(const Programme &p, int64_t now) {
        return now >= p.stop ? 0 : (int) ((p.stop - now + 59) / 60);
    }

    // ---------------------------------------------------------------------------------------------------- Cache

    Cache::Entry &Cache::touch(const std::string &channelId) {
        Entry &e = entries[channelId];
        e.used = ++clock;
        trim();
        return entries[channelId];
    }

    void Cache::trim() {
        while (entries.size() > MAX_CHANNELS) {
            auto oldest = entries.begin();
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                if (it->second.used < oldest->second.used) {
                    oldest = it;
                }
            }
            entries.erase(oldest);
        }
    }

    const Cache::Entry &Cache::get(const std::string &channelId) {
        auto it = entries.find(channelId);
        if (it == entries.end()) {
            return missing;
        }
        it->second.used = ++clock;
        return it->second;
    }

    bool Cache::due(const std::string &channelId, int64_t now) const {
        auto it = entries.find(channelId);
        return it == entries.end() || now >= it->second.until;
    }

    void Cache::loading(const std::string &channelId, int64_t now) {
        Entry &e = touch(channelId);
        if (e.state == State::Missing || e.state == State::Failed) {
            e.state = State::Loading;   // an older answer (Ready / Empty) stays shown while it is asked again
        }
        e.until = now + LOADING_FOR;
    }

    void Cache::store(const std::string &channelId, std::vector<Programme> programmes, int64_t now) {
        Entry &e = touch(channelId);
        programmes.erase(std::remove_if(programmes.begin(), programmes.end(),
                                        [now](const Programme &p) { return p.stop <= now; }), programmes.end());
        if (programmes.empty()) {
            e.state = State::Empty;
            e.programmes.clear();
            e.until = now + EMPTY_FOR;
            return;
        }
        // asked again when the programme on air ends (or the first one starts), within MIN_READY_FOR..READY_FOR
        NowNext nn = nowNext(programmes, now);
        int64_t change = nn.now >= 0 ? programmes[(size_t) nn.now].stop : programmes.front().start;
        e.state = State::Ready;
        e.programmes = std::move(programmes);
        e.until = std::min(now + READY_FOR, std::max(now + MIN_READY_FOR, change));
    }

    void Cache::failed(const std::string &channelId, int64_t now) {
        Entry &e = touch(channelId);
        if (e.state != State::Ready && e.state != State::Empty) {
            e.state = State::Failed;   // an older answer stays shown; it is asked again soon either way
        }
        e.until = now + FAILED_FOR;
    }
}
