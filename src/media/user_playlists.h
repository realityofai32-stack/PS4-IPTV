// My Playlists: the user's own playlists, made and edited in the app (host-testable; screens/my_playlists_screen.cpp
// is the UI).
//
// A playlist is an ordinary Extended M3U file in the app's local playlist folder, /data/PS4IPTV/playlists/ (the folder
// a playlist source can already be opened from, m3u::localPlaylistPath):
//
//   #EXTM3U
//   #PLAYLIST:My Sports                    (the name shown in the app; the file name stays as it was created)
//   #EXTINF:-1 tvg-id="..." tvg-logo="..." group-title="...",Channel name
//   #EXTVLCOPT:http-user-agent=...         (only when the entry needs its own User-Agent)
//   http://host/stream.ts
//
// So a playlist survives restarts as it is, opens as a playlist source (Live TV, the live player and its channel list)
// without any conversion, and imports / exports as a normal M3U file. Files copied into the folder over FTP are listed
// too; one with more than MAX_ENTRIES entries (a provider's whole playlist) is not edited here, only used as a source.
//
// Entries are a name and a stream address (http / https / rtmp / rtsp / rtp / udp / mms): nothing is downloaded or
// checked when an entry is added. Writes go through fs::writeFileAtomic (never a half-written playlist).

#ifndef PS4IPTV_MEDIA_USER_PLAYLISTS_H
#define PS4IPTV_MEDIA_USER_PLAYLISTS_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace userpl {

    constexpr size_t MAX_ENTRIES = 5000;         // entries a playlist may have to be edited here
    constexpr size_t MAX_FILE_BYTES = 2u << 20;  // files read at most (listing, editing, import)
    constexpr size_t MAX_PLAYLISTS = 200;        // files listed at most
    constexpr size_t MAX_NAME = 120;             // bytes: playlist and entry names
    constexpr size_t MAX_URL = 4096;

    struct Entry {
        std::string name;
        std::string url;
        std::string logo;
        std::string group;
        std::string tvgId;
        std::string userAgent;
        // added from a source's channel: that source (profile id) and channel id (v7-source / v7-channel). In that
        // source's Live TV the entry is the source's own channel (app/user_groups.h); elsewhere its address plays.
        std::string sourceId;
        std::string channelId;
    };

    struct Playlist {
        std::string path;     // the file
        std::string title;
        std::vector<Entry> entries;
    };

    struct Summary {
        std::string path;
        std::string file;      // "my-sports.m3u"
        std::string title;
        int entries = 0;
        bool editable = true;  // false: too large / not readable as a playlist (listed, used only as a source)
        int64_t size = 0;
    };

    // ------------------------------------------------------------------ text
    // M3U text -> playlist (title: #PLAYLIST, else `fallbackTitle`). An empty playlist ("#EXTM3U" alone) is valid.
    // false: not a playlist (HTML, JSON, an HLS stream...), or more than MAX_ENTRIES entries
    bool fromM3u(std::string_view text, const std::string &fallbackTitle, Playlist &out, std::string &error);

    std::string toM3u(const Playlist &playlist);

    // one line, white space runs as one space, trimmed, at most MAX_NAME bytes (cut at a character boundary)
    std::string cleanName(const std::string &name);

    // a stream address an entry may hold: a known streaming scheme with a host, no white space or control
    // characters, at most MAX_URL bytes
    bool validUrl(const std::string &url);

    // "My Sports!" -> "my-sports" (ASCII letters / digits, '-' between words; "playlist" when nothing is left)
    std::string slug(const std::string &title);

    // ------------------------------------------------------------------ files
    // the playlists of `dir` (*.m3u / *.m3u8, symbolic links skipped), by title
    std::vector<Summary> list(const std::string &dir);

    bool load(const std::string &path, Playlist &out, std::string &error);

    bool save(const Playlist &playlist, std::string &error);

    bool remove(const std::string &path);

    // a file in `dir` for a new playlist: "<slug>.m3u", "<slug>-2.m3u"... (never an existing file)
    std::string newPath(const std::string &dir, const std::string &title);

    // `path` is a playlist file directly inside `dir` (no other folder, no "..", .m3u / .m3u8)
    bool inside(const std::string &dir, const std::string &path);

    // ------------------------------------------------------------------ edits
    // moves entry `index` by `delta` places (clamped); returns its new index (-1: no such entry)
    int move(Playlist &playlist, int index, int delta);

    // the entry with this address (-1 = none)
    int find(const Playlist &playlist, const std::string &url);
}

#endif // PS4IPTV_MEDIA_USER_PLAYLISTS_H
