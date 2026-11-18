// XMLTV programme guides of playlist (M3U) sources (host-testable: no networking, no decompression here).
//
// A playlist names its guide in its header (#EXTM3U url-tvg="..." / x-tvg-url="..."): an XMLTV file, often tens
// of MB, often gzip-compressed. It is never held in memory: app/xmltv_service.cpp streams the download through
// network/gunzip.h into a Reader, which hands each <channel> and <programme> to a Builder. The Builder keeps only
// what the playlist can use - channels it matches, programmes still running or starting within the next
// BuildLimits::keepAfter - within fixed memory limits, and writes a compact saved guide file. An Index opens that
// file (its channel table only) and reads one channel's programmes from it when the guide panel shows the channel.
//
// Matching (Index::find): a playlist channel's tvg-id against the XMLTV channel ids, as given, then ignoring ASCII
// case; when that finds nothing (or the channel has no usable tvg-id), its name against the guide's display
// names (folded like search: case and Latin diacritics ignored), only when exactly one guide channel has that
// name. Nothing else: no partial names, no guessing.
//
// Nothing is made up: a programme needs a valid start time and a title. A missing / unreadable stop ends it at the
// channel's next programme in the guide (at most epg::MAX_DURATION later), otherwise it is dropped. Times without
// a zone are UTC (the XMLTV rule); an unknown zone drops the programme. Overlaps and duplicates are handled like
// the providers' short guides (epg.h).

#ifndef PS4IPTV_IPTV_XMLTV_H
#define PS4IPTV_IPTV_XMLTV_H

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "catalog.h"
#include "epg.h"

namespace xmltv {

    // "20251117200000 +0300" (seconds optional, zone "+HHMM" / "-HHMM" / "+HH:MM" / "Z" / "UTC" / "GMT" or none =
    // UTC) -> unix time. false for anything else.
    bool parseTime(std::string_view text, int64_t &out);

    // name used to match a playlist channel with a guide channel: folded like search (case, Latin diacritics),
    // runs of white space as one space, trimmed
    std::string nameKey(const std::string &name);

    // ------------------------------------------------------------------ streaming reader

    struct Channel {
        std::string id;
        std::vector<std::string> names;   // display-name values (at most 4)
    };

    struct Programme {
        std::string channel;
        int64_t start = 0;
        int64_t stop = 0;                 // 0: none given (or unreadable)
        std::string title;                // raw text (entities decoded), the preferred language's when there are several
        std::string description;
    };

    class Handler {

    public:

        virtual ~Handler() = default;

        virtual void channel(Channel &channel) = 0;

        virtual void programme(Programme &programme) = 0;
    };

    struct ReaderStats {
        size_t bytes = 0;            // XML bytes read
        int channels = 0;            // <channel> elements
        int programmes = 0;          // <programme> elements
        int badTimes = 0;            // programmes without a readable start (dropped)
        int noTitle = 0;             // programmes without a title (dropped)
        int oversized = 0;           // tags / text nodes longer than Reader::MAX_TOKEN (skipped)
    };

    // A forgiving streaming XML reader for the XMLTV elements the guide uses (tv / channel / display-name /
    // programme / title / desc); everything else is skipped. Comments, CDATA, the XML declaration (UTF-8, or
    // ISO-8859-1 / Windows-1252, converted) and DOCTYPE are understood; entities (&amp; &#233; &#xE9; ...) are
    // decoded. Memory stays bounded whatever the input: one token at a time, at most MAX_TOKEN bytes.
    class Reader {

    public:

        static constexpr size_t MAX_TOKEN = 256 * 1024;
        static constexpr size_t MAX_FIELD = 8 * 1024;   // title / description text kept before cleaning

        // preferredLanguage: "tr" / "en"...: a title / description in that language wins over the first one
        explicit Reader(Handler &handler, std::string preferredLanguage = std::string());

        // false: not an XMLTV document (another root element, text before the root): stop feeding
        bool feed(const char *data, size_t size);

        // the end of the data: true when it was an XMLTV document (a <tv> root)
        bool finish();

        const ReaderStats &stats() const { return st; }

    private:

        enum class Mode {
            Normal,
            Comment,
            Cdata,
            SkipText,   // an oversized text node: to the next '<'
            SkipTag     // an oversized tag: to the next '>'
        };

        enum class Field {
            None,
            DisplayName,
            Title,
            Description
        };

        void process(bool final);

        void text(std::string_view raw, bool cdata);

        void startTag(std::string_view tag);

        void endTag(std::string_view name);

        void instruction(std::string_view body);

        std::string decode(std::string_view raw, bool entities) const;

        bool attribute(std::string_view tag, std::string_view name, std::string &value) const;

        Handler &handler;
        std::string language;
        std::string buf;
        size_t pos = 0;
        Mode mode = Mode::Normal;
        bool started = false;      // the root element was seen
        bool rootTv = false;
        bool broken = false;
        bool latin1 = false;       // the declaration names ISO-8859-1 / Windows-1252
        std::vector<std::string> stack;
        Field field = Field::None;
        std::string fieldText;
        std::string fieldLanguage;
        bool inChannel = false;
        bool inProgramme = false;
        bool programmeValid = false;
        bool titleLanguageMatched = false;
        bool descriptionLanguageMatched = false;
        Channel ch;
        Programme pr;
        ReaderStats st;
    };

    // ------------------------------------------------------------------ guide building

    // what a playlist can match (copied from the catalog on the main thread before a download)
    struct Wanted {
        std::unordered_set<std::string> ids;        // usable tvg-ids as given
        std::unordered_set<std::string> lowerIds;   // ... ASCII lower-case
        std::unordered_set<std::string> names;      // nameKey of the channel names

        static Wanted of(const iptv::LiveCatalog &live);

        // one playlist channel: its tvg-id (when usable) and its name
        void add(const std::string &tvgId, const std::string &name);

        bool empty() const { return ids.empty() && names.empty(); }
    };

    struct BuildLimits {
        int64_t keepAfter = 36 * 3600;     // programmes starting later than now + this are not kept
        size_t maxPerChannel = 48;         // programmes kept per channel (the first ones from now)
        size_t maxProgrammes = 200000;     // programmes held while building
        size_t maxTextBytes = 16u << 20;   // title + description bytes held while building (descriptions: 3/4)
        size_t maxChannels = 20000;        // guide channels kept
        size_t maxKnownChannels = 200000;  // guide channels remembered as not wanted
        size_t maxDescription = 480;       // bytes per description (the panel shows a few lines)
    };

    struct BuildStats {
        int matchedChannels = 0;     // guide channels the playlist matches
        int unmatchedProgrammes = 0; // for channels the playlist does not have
        int outsideWindow = 0;       // ended already / start after keepAfter
        int kept = 0;                // programmes written
        int dropped = 0;             // no stop and no next programme, duplicates, over the per-channel limit
        int overLimit = 0;           // not held: maxProgrammes / maxTextBytes / maxChannels reached
        int descriptionsDropped = 0; // descriptions not kept because of maxTextBytes
        int channelsWritten = 0;
        int64_t coverageEnd = 0;     // the latest stop written
    };

    class Builder : public Handler {

    public:

        Builder(Wanted wanted, int64_t now, BuildLimits limits = BuildLimits());

        void channel(Channel &c) override;

        void programme(Programme &p) override;

        // completes the programmes (sorted, stops, overlaps, per-channel limit) and writes the guide file
        // atomically (<path>.tmp renamed into place). sourceKey: what the guide was built from (Index::sourceKey).
        bool write(const std::string &path, uint64_t sourceKey, std::string &error);

        const BuildStats &stats() const { return bs; }

        size_t held() const { return records.size(); }

    private:

        struct Entry {
            std::string id;
            std::vector<std::string> names;   // nameKey values
        };

        struct Record {
            uint32_t channel;
            uint32_t text;        // offset into `arena`: title, then description
            int64_t start;
            int64_t stop;
            uint16_t titleLength;
            uint16_t descriptionLength;
        };

        int channelOf(const std::string &id);

        bool wantedId(const std::string &id) const;

        Wanted wanted;
        int64_t now;
        BuildLimits limits;
        std::vector<Entry> entries;
        std::unordered_map<std::string, int> known;   // guide channel id -> entry, -1 = not wanted
        std::vector<Record> records;
        std::string arena;
        BuildStats bs;
    };

    // ------------------------------------------------------------------ the saved guide

    // A saved guide file: the channel table in memory (ids, names, where each channel's programmes are), the
    // programmes read per channel from the file. Immutable once opened: shared between threads.
    class Index {

    public:

        static constexpr uint32_t VERSION = 1;

        // false: missing, unreadable, damaged or written by another version
        bool open(const std::string &path, std::string &error);

        // the guide channel of a playlist channel (see the header comment), -1 = none
        int find(const iptv::LiveChannel &channel) const;

        // that channel's programmes still running or to come at `now` (at most epg::MAX_PROGRAMMES). gone: the file
        // is missing, or is no longer the one this Index opened (a newer guide replaced it: its offsets would point
        // at other programmes, so nothing is read)
        bool read(int channel, int64_t now, std::vector<epg::Programme> &out, std::string &error,
                  bool *gone = nullptr) const;

        size_t size() const { return slots.size(); }

        int64_t savedAt = 0;
        int64_t coverageEnd = 0;    // the latest stop in the guide
        uint64_t sourceKey = 0;

    private:

        struct Slot {
            uint64_t offset = 0;
            uint32_t bytes = 0;
            uint32_t count = 0;
        };

        std::string path;
        std::string header;        // the file's header when opened: a read checks the file is still this one
        std::vector<Slot> slots;
        std::unordered_map<std::string, int> byId;
        std::unordered_map<std::string, int> byLowerId;
        std::unordered_map<std::string, int> byName;   // -1: more than one guide channel has the name
    };

    // what a guide is built from: the playlist's guide addresses (an Index built from other addresses is not used)
    uint64_t sourceKey(const std::vector<std::string> &guideUrls);
}

#endif // PS4IPTV_IPTV_XMLTV_H
