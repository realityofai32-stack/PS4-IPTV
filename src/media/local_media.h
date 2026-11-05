// Local Media: browsing media files on the console's storage (host-testable; screens/local_media_screen.cpp is the
// UI). Not a file manager: read-only folder listing, media files only, inside fixed roots.
//
// Roots: /data/ (the app-accessible user storage, where files copied over FTP live, incl. /data/PS4IPTV/media/) and
// USB storage at /mnt/usb0/../mnt/usb7/. A root is offered only when it can be opened right now (USB drives come
// and go; access to /mnt/usbN depends on the console's homebrew setup).
//
// Safety: every path is normalized and must stay inside its root: ".." / "." segments, empty names, names with a
// slash and symbolic links are never followed, so browsing cannot leave the roots (no path traversal).
//
// Playable files: decided by the linked FFmpeg, not by a hand-written list. The extensions of every demuxer in the
// build (av_demuxer_iterate) are collected, minus the demuxers that do not make a video or audio file on their own
// (subtitles, images, text, playlists, metadata): Formats::excludedDemuxers(). Formats FFmpeg recognizes by probing
// (MPEG-TS, MPEG-PS, ASF, WAV...) add their usual extensions when their demuxer is in the build.

#ifndef PS4IPTV_MEDIA_LOCAL_MEDIA_H
#define PS4IPTV_MEDIA_LOCAL_MEDIA_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace localmedia {

    struct Root {
        std::string path;        // "/data/", "/mnt/usb0/" (trailing slash)
        bool usb = false;
        int number = 0;          // USB: 1..8
    };

    // the roots that may exist on the console, in display order. base: a directory standing in for the file system
    // root in tests ("" on the console)
    std::vector<Root> candidateRoots(const std::string &base = "");

    // the candidate roots that can be opened right now
    std::vector<Root> detectRoots(const std::string &base = "");

    // "/data//a/./b/" -> "/data/a/b"; false for relative paths and any ".." segment
    bool normalize(const std::string &path, std::string &out);

    // `path` (any form) is `root` itself or inside it, after normalization
    bool within(const Root &root, const std::string &path);

    // the root `path` belongs to (nullptr when none)
    const Root *rootOf(const std::vector<Root> &roots, const std::string &path);

    // a directory entry name that may be joined to its folder: not empty, no '/', not "." / "..", no NUL
    bool safeName(const std::string &name);

    // "Movie.Name.MKV" -> "mkv" ("" when none)
    std::string extensionOf(const std::string &name);

    class Formats {

    public:

        // demuxers that never make a playable file by themselves
        static const std::set<std::string> &excludedDemuxers();

        // demuxer name -> the usual extensions of a format FFmpeg recognizes by probing (no list of its own)
        static const std::map<std::string, std::string> &probedExtensions();

        // one demuxer of the linked FFmpeg: name ("matroska,webm") and extensions ("mkv,mk3d,mka,mks")
        void addDemuxer(const std::string &name, const std::string &extensions);

        bool supported(const std::string &extension) const;

        size_t size() const { return exts.size(); }

        const std::set<std::string> &extensions() const { return exts; }

    private:

        std::set<std::string> exts;
    };

    // the linked FFmpeg's formats (PS4 build; empty on the host)
    Formats ffmpegFormats();

    struct Entry {
        std::string name;
        std::string path;     // folder + "/" + name
        bool directory = false;
        int64_t size = -1;    // files only
    };

    // folders first, then playable files, each in natural order ("Episode 2" before "Episode 10"), case-insensitive;
    // hidden entries (".name"), unsafe names and unsupported files are dropped
    std::vector<Entry> prepare(std::vector<Entry> raw, const Formats &formats);

    // entries one folder shows at most (folders and playable files; what is skipped does not count)
    constexpr size_t MAX_ENTRIES = 5000;

    struct Listing {
        bool ok = false;
        std::vector<Entry> entries;
        int skipped = 0;      // hidden, unsafe, unsupported, symbolic links, special files
        bool truncated = false;   // more than MAX_ENTRIES entries to show
    };

    // reads one folder inside `root` (symbolic links are skipped, never followed)
    Listing list(const Root &root, const std::string &folder, const Formats &formats);

    // the root can be opened right now
    bool available(const Root &root);

    // "a" < "b", "Episode 2" < "Episode 10", case-insensitive (ASCII)
    bool naturalLess(const std::string &a, const std::string &b);
}

#endif // PS4IPTV_MEDIA_LOCAL_MEDIA_H
