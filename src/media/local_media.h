// Local Media: browsing media files on the console's storage (host-testable; screens/local_media_screen.cpp is the
// UI). Not a file manager: read-only folder listing, media files only, inside fixed roots.
//
// Roots: /data/ (the app-accessible user storage, where files copied over FTP live, incl. /data/PS4IPTV/media/) and
// USB storage. A root is offered only when it can be opened right now (USB drives come and go).
//
// Optical discs (probe only, separate from the USB logic): the places a disc could appear for the app - "/disc"
// inside the sandbox, "/mnt/disc" in the outside view - are looked at read-only (stat + opendir) like USB mount points
// (an empty folder: no disc). A readable disc is browsed exactly like a USB drive: media files only, no symbolic
// links, never outside it. A protected Blu-ray movie (an AACS folder at its top) is never read further: such discs
// play only in the console's own Blu-ray player (no copy protection is ever touched). Hardware tests decide whether a
// data disc is visible to the app at all; the startup log names what "/" holds.
//
// USB paths (v0.4.1): an application runs in the console's file system sandbox, where USB drives are /usb0../usb7
// (and /data is the console's /user/data). /mnt/usb0../mnt/usb7 is the view from outside the sandbox (FTP, a
// process that escaped it): v0.4.0 looked only there, so a sandboxed app never found a drive. Both are probed, the
// sandbox path first. The two views are never mixed (a drive could otherwise be listed twice, under two numbers):
// /mnt/usbN is used only when no /usbN holds a drive. A mount point without a drive is an empty folder: a USB root is
// offered only when its folder lists at least one entry.
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
        std::string path;        // "/data/", "/usb0/", "/mnt/usb0/" (trailing slash)
        bool usb = false;
        int number = 0;          // USB: 1..8
        bool outside = false;    // USB / disc: the view from outside the sandbox (/mnt/...; else the app's view)
        bool disc = false;       // an optical disc
    };


    // the roots that may exist on the console, in display order. base: a directory standing in for the file system
    // root in tests ("" on the console)
    std::vector<Root> candidateRoots(const std::string &base = "");

    // the candidate roots that can be opened right now: /data/, then one root per USB drive number
    std::vector<Root> detectRoots(const std::string &base = "");

    // why a root can or cannot be opened: tells "no drive mounted there" apart from "mounted, but this app may not
    // read it" (the console's sandbox / permissions) in the log and on screen
    enum class Access {
        Available,      // opened
        Missing,        // nothing at that path (ENOENT / ENOTDIR): no drive mounted, or not visible to the app
        Empty,          // USB: an empty folder, the mount point of a drive that is not plugged in
        Denied,         // exists but may not be read (EACCES / EPERM)
        NotDirectory,   // exists and is not a folder
        Error,          // another error (errno kept)
    };

    struct Probe {
        Root root;
        Access access = Access::Missing;
        int error = 0;      // errno of the failing call (0 when available)
    };

    const char *accessName(Access a);

    // every candidate root with its access; nothing is created, written or followed (stat + opendir only)
    std::vector<Probe> probeRoots(const std::string &base = "");

    // the available roots of a probe, in display order: /data/, then USB 1..8. One view only, so every drive is listed
    // once: the sandbox's /usbN drives, or (when no /usbN holds a drive) the /mnt/usbN ones.
    std::vector<Root> rootsFrom(const std::vector<Probe> &probes);

    // optical discs, apart from the USB logic: "/disc/" (the app's view), then "/mnt/disc/" (outside view)
    std::vector<Root> discCandidates(const std::string &base = "");

    // the disc candidates as probed (an empty folder: no disc), for the log
    std::vector<Probe> probeDiscs(const std::string &base = "");

    // the disc that can be opened right now (none, or the first readable candidate that lists something)
    std::vector<Root> discsFrom(const std::vector<Probe> &probes);

    // a protected Blu-ray movie: an "AACS" folder at the disc's top (checked before anything else is read)
    bool protectedBluray(const Root &disc);

    // "data ok, usb0 missing, ..., mnt/usb1 denied (13), ..." for the log
    std::string probeSummary(const std::vector<Probe> &probes);

    // the entries of a system folder ("/" or "/mnt": mount point names such as "usb0", "data", "app0"), for the log.
    // Only names shaped like the console's own mount points are printed (1-24 characters: lower-case letters, digits,
    // '_', '-', '.'); anything else (upper case, spaces, the sandbox's random word...) is counted, never printed.
    // At most 32 names.
    struct MountList {
        std::string dir;                  // "/", "/mnt"
        bool ok = false;
        int error = 0;                    // errno when the folder could not be read
        std::vector<std::string> names;   // sorted
        int hidden = 0;                   // names not shown (unusual characters, or past the cap)
    };

    // dir: "/" or "/mnt" (under base)
    MountList mountPoints(const std::string &base = "", const std::string &dir = "/mnt");

    std::string mountSummary(const MountList &m);

    // what the storage list says about USB drives when none can be opened
    enum class UsbHint {
        None,       // at least one USB drive is available
        NoDrive,    // every USB path is missing or an empty mount point
        Denied,     // a USB path exists but cannot be read (and no USB mount point is readable)
    };

    UsbHint usbHint(const std::vector<Probe> &probes);

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

    // directory entries one listing reads at most (everything included: a folder holding hundreds of thousands of
    // files must not freeze the screen; the listing is then flagged as truncated)
    constexpr size_t MAX_SCANNED = 50000;

    struct Listing {
        bool ok = false;
        std::vector<Entry> entries;
        int skipped = 0;      // hidden, unsafe, unsupported, symbolic links, special files
        bool truncated = false;   // more than MAX_ENTRIES entries to show, or more than MAX_SCANNED entries
        int error = 0;            // !ok: errno of the failing opendir (EINVAL: refused, outside the root)
    };

    // reads one folder inside `root`. Symbolic links are skipped, never followed: not as entries, and not as a folder
    // on the way from the root to `folder` either (refused with ELOOP) - even when asked for such a path directly.
    Listing list(const Root &root, const std::string &folder, const Formats &formats);

    // the root can be opened right now
    bool available(const Root &root);

    // "a" < "b", "Episode 2" < "Episode 10", case-insensitive (ASCII)
    bool naturalLess(const std::string &a, const std::string &b);
}

#endif // PS4IPTV_MEDIA_LOCAL_MEDIA_H
