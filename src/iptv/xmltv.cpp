#include <algorithm>
#include <cstdio>

#include "m3u.h"
#include "xmltv.h"
#include "../core/utf8.h"

namespace xmltv {

    namespace {
        const char MAGIC[8] = {'V', '7', 'X', 'M', 'L', 'T', 'V', 0};
        const size_t HEADER_BYTES = 64;
        const uint64_t MAX_TABLE_BYTES = 64u << 20;
        const uint32_t MAX_BLOCK_BYTES = 1u << 20;

        bool isSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        std::string_view trim(std::string_view s) {
            while (!s.empty() && isSpace(s.front())) {
                s.remove_prefix(1);
            }
            while (!s.empty() && isSpace(s.back())) {
                s.remove_suffix(1);
            }
            return s;
        }

        std::string lowerAscii(std::string_view s) {
            std::string out(s);
            for (char &c: out) {
                if (c >= 'A' && c <= 'Z') {
                    c = (char) (c - 'A' + 'a');
                }
            }
            return out;
        }

        void appendUtf8(std::string &out, uint32_t cp) {
            if (cp < 0x80) {
                out += (char) cp;
            } else if (cp < 0x800) {
                out += (char) (0xC0 | (cp >> 6));
                out += (char) (0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += (char) (0xE0 | (cp >> 12));
                out += (char) (0x80 | ((cp >> 6) & 0x3F));
                out += (char) (0x80 | (cp & 0x3F));
            } else {
                out += (char) (0xF0 | (cp >> 18));
                out += (char) (0x80 | ((cp >> 12) & 0x3F));
                out += (char) (0x80 | ((cp >> 6) & 0x3F));
                out += (char) (0x80 | (cp & 0x3F));
            }
        }

        // Windows-1252 0x80..0x9F (0 = undefined); ISO-8859-1 maps the other bytes to the same code point
        const uint16_t CP1252[32] = {
                0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,
                0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A,
                0x0153, 0, 0x017E, 0x0178};

        bool leapYear(int y) {
            return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        }

        int daysInMonth(int y, int m) {
            static const int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return m == 2 && leapYear(y) ? 29 : DAYS[m - 1];
        }

        // days since 1970-01-01 (proleptic Gregorian)
        int64_t daysFromCivil(int y, int m, int d) {
            y -= m <= 2;
            int64_t era = (y >= 0 ? y : y - 399) / 400;
            int64_t yoe = y - era * 400;
            int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + doe - 719468;
        }

        uint64_t fnv(const std::string &s, uint64_t h = 1469598103934665603ull) {
            for (unsigned char c: s) {
                h ^= c;
                h *= 1099511628211ull;
            }
            return h;
        }

        // little-endian fields of the saved guide
        void put(std::string &out, uint64_t v, int bytes) {
            for (int i = 0; i < bytes; i++) {
                out += (char) ((v >> (8 * i)) & 0xFF);
            }
        }

        void putText(std::string &out, const std::string &s, size_t cap) {
            size_t n = std::min(s.size(), cap);
            put(out, n, 2);
            out.append(s, 0, n);
        }

        struct Cursor {
            const std::string &data;
            size_t at = 0;
            bool ok = true;

            uint64_t get(int bytes) {
                if (!ok || at + (size_t) bytes > data.size()) {
                    ok = false;
                    return 0;
                }
                uint64_t v = 0;
                for (int i = 0; i < bytes; i++) {
                    v |= (uint64_t) (unsigned char) data[at + (size_t) i] << (8 * i);
                }
                at += (size_t) bytes;
                return v;
            }

            std::string text() {
                size_t n = (size_t) get(2);
                if (!ok || at + n > data.size()) {
                    ok = false;
                    return std::string();
                }
                std::string s = data.substr(at, n);
                at += n;
                return s;
            }
        };

        bool languageMatches(const std::string &lang, const std::string &preferred) {
            if (preferred.empty() || lang.size() < preferred.size()) {
                return false;
            }
            return lang.compare(0, preferred.size(), preferred) == 0
                   && (lang.size() == preferred.size() || lang[preferred.size()] == '-' || lang[preferred.size()] == '_');
        }
    }

    // ---------------------------------------------------------------------------------------------- helpers

    bool parseTime(std::string_view t, int64_t &out) {
        t = trim(t);
        size_t digits = 0;
        while (digits < t.size() && t[digits] >= '0' && t[digits] <= '9') {
            digits++;
        }
        if (digits != 12 && digits != 14) {
            return false;
        }
        auto num = [&](size_t at, size_t len) {
            int v = 0;
            for (size_t k = 0; k < len; k++) {
                v = v * 10 + (t[at + k] - '0');
            }
            return v;
        };
        int y = num(0, 4);
        int mo = num(4, 2);
        int d = num(6, 2);
        int h = num(8, 2);
        int mi = num(10, 2);
        int s = digits == 14 ? num(12, 2) : 0;
        if (y < 1970 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > daysInMonth(y, mo) || h > 23 || mi > 59
            || s > 59) {
            return false;
        }
        std::string_view z = trim(t.substr(digits));
        int offset = 0;   // minutes east of UTC
        if (!z.empty() && z != "Z" && z != "UTC" && z != "GMT") {
            if (z[0] != '+' && z[0] != '-') {
                return false;   // a zone name ("EST"): unknown, never guessed
            }
            std::string_view v = z.substr(1);
            int hh = 0;
            int mm = 0;
            auto two = [](std::string_view p, int &value) {
                if (p.size() != 2 || p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') {
                    return false;
                }
                value = (p[0] - '0') * 10 + (p[1] - '0');
                return true;
            };
            bool ok = (v.size() == 4 && two(v.substr(0, 2), hh) && two(v.substr(2, 2), mm))
                      || (v.size() == 5 && v[2] == ':' && two(v.substr(0, 2), hh) && two(v.substr(3, 2), mm))
                      || (v.size() == 2 && two(v, hh));
            if (!ok || hh > 14 || mm > 59) {
                return false;
            }
            offset = (hh * 60 + mm) * (z[0] == '-' ? -1 : 1);
        }
        out = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s - (int64_t) offset * 60;
        return true;
    }

    std::string nameKey(const std::string &name) {
        std::u32string folded = utf8::foldForSearch(name);
        std::string out;
        bool space = false;
        for (char32_t c: folded) {
            if (c <= 0x20 || c == 0xA0 || c == 0x7F) {
                space = !out.empty();
                continue;
            }
            if (space) {
                out += ' ';
                space = false;
            }
            out += utf8::encode(c);
        }
        return out;
    }

    uint64_t sourceKey(const std::vector<std::string> &guideUrls) {
        std::string joined = "xmltv-v" + std::to_string(Index::VERSION);
        for (const auto &u: guideUrls) {
            joined += '\n';
            joined += u;
        }
        return fnv(joined);
    }

    // ---------------------------------------------------------------------------------------------- Reader

    Reader::Reader(Handler &h, std::string preferredLanguage) : handler(h), language(lowerAscii(preferredLanguage)) {}

    bool Reader::feed(const char *data, size_t size) {
        if (broken) {
            return false;
        }
        st.bytes += size;
        buf.append(data, size);
        process(false);
        if (pos > 0) {
            buf.erase(0, pos);
            pos = 0;
        }
        return !broken;
    }

    bool Reader::finish() {
        if (!broken) {
            process(true);
        }
        buf.clear();
        pos = 0;
        return !broken && rootTv;
    }

    void Reader::process(bool final) {
        while (!broken && pos < buf.size()) {
            switch (mode) {
                case Mode::Comment: {
                    size_t e = buf.find("-->", pos);
                    if (e == std::string::npos) {
                        pos = std::max(pos, buf.size() >= 2 ? buf.size() - 2 : 0);
                        return;
                    }
                    pos = e + 3;
                    mode = Mode::Normal;
                    continue;
                }
                case Mode::Cdata: {
                    size_t e = buf.find("]]>", pos);
                    if (e == std::string::npos) {
                        size_t upto = buf.size() >= 2 ? buf.size() - 2 : 0;
                        if (upto > pos) {
                            text(std::string_view(buf).substr(pos, upto - pos), true);
                            pos = upto;
                        }
                        return;
                    }
                    text(std::string_view(buf).substr(pos, e - pos), true);
                    pos = e + 3;
                    mode = Mode::Normal;
                    continue;
                }
                case Mode::SkipText: {
                    size_t lt = buf.find('<', pos);
                    if (lt == std::string::npos) {
                        pos = buf.size();
                        return;
                    }
                    pos = lt;
                    mode = Mode::Normal;
                    continue;
                }
                case Mode::SkipTag: {
                    size_t gt = buf.find('>', pos);
                    if (gt == std::string::npos) {
                        pos = buf.size();
                        return;
                    }
                    pos = gt + 1;
                    mode = Mode::Normal;
                    continue;
                }
                case Mode::Normal:
                    break;
            }
            if (buf[pos] != '<') {
                size_t lt = buf.find('<', pos);
                if (lt == std::string::npos && !started && !final) {
                    // before the root element only white space may wait for more data: anything else (JSON, an error
                    // text, binary) is not XML - rejected with this piece, not after a download of the whole answer
                    static const std::string_view BOM("\xEF\xBB\xBF", 3);
                    std::string_view rest = std::string_view(buf).substr(pos);
                    if (rest.size() < BOM.size() && BOM.substr(0, rest.size()) == rest) {
                        return;   // perhaps a byte order mark split between two pieces
                    }
                    text(rest, false);
                    pos = buf.size();
                    return;
                }
                if (lt == std::string::npos) {
                    if (final) {
                        text(std::string_view(buf).substr(pos), false);
                        pos = buf.size();
                    } else if (buf.size() - pos > MAX_TOKEN) {
                        st.oversized++;
                        mode = Mode::SkipText;
                        pos = buf.size();
                    }
                    return;   // the rest of the text node comes with the next data
                }
                text(std::string_view(buf).substr(pos, lt - pos), false);
                pos = lt;
                continue;
            }
            size_t avail = buf.size() - pos;
            if (avail < 9 && !final && avail >= 2 && buf[pos + 1] == '!') {
                return;   // may still become "<!--" / "<![CDATA["
            }
            if (buf.compare(pos, 4, "<!--") == 0) {
                pos += 4;
                mode = Mode::Comment;
                continue;
            }
            if (buf.compare(pos, 9, "<![CDATA[") == 0) {
                pos += 9;
                mode = Mode::Cdata;
                continue;
            }
            // the end of the tag: '>' outside quotes (and, in a declaration, outside [ ... ])
            bool declaration = avail >= 2 && buf[pos + 1] == '!';
            char quote = 0;
            int brackets = 0;
            size_t gt = std::string::npos;
            for (size_t i = pos + 1; i < buf.size(); i++) {
                char c = buf[i];
                if (quote) {
                    if (c == quote) {
                        quote = 0;
                    }
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (declaration && c == '[') {
                    brackets++;
                } else if (declaration && c == ']') {
                    brackets--;
                } else if (c == '>' && brackets <= 0) {
                    gt = i;
                    break;
                }
            }
            if (gt == std::string::npos) {
                if (buf.size() - pos > MAX_TOKEN) {
                    st.oversized++;
                    mode = Mode::SkipTag;
                    pos = buf.size();
                }
                return;
            }
            std::string_view tag = std::string_view(buf).substr(pos + 1, gt - pos - 1);
            pos = gt + 1;
            if (tag.empty()) {
                continue;
            }
            if (tag[0] == '?') {
                instruction(tag);
            } else if (tag[0] == '!') {
                // DOCTYPE and other declarations: nothing the guide needs
            } else if (tag[0] == '/') {
                endTag(trim(tag.substr(1)));
            } else {
                startTag(tag);
            }
        }
    }

    void Reader::instruction(std::string_view body) {
        if (body.compare(0, 4, "?xml") != 0) {
            return;
        }
        std::string enc;
        if (attribute(body.substr(1), "encoding", enc)) {
            enc = lowerAscii(enc);
            latin1 = enc == "iso-8859-1" || enc == "latin1" || enc == "latin-1" || enc == "iso-8859-15"
                     || enc == "windows-1252" || enc == "cp1252";
        }
    }

    std::string Reader::decode(std::string_view raw, bool entities) const {
        std::string out;
        out.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); i++) {
            unsigned char c = (unsigned char) raw[i];
            if (c == '&' && entities) {
                size_t semi = raw.find(';', i + 1);
                if (semi != std::string_view::npos && semi - i <= 12) {
                    std::string_view name = raw.substr(i + 1, semi - i - 1);
                    uint32_t cp = 0;
                    if (name == "amp") {
                        cp = '&';
                    } else if (name == "lt") {
                        cp = '<';
                    } else if (name == "gt") {
                        cp = '>';
                    } else if (name == "quot") {
                        cp = '"';
                    } else if (name == "apos") {
                        cp = '\'';
                    } else if (name == "nbsp") {
                        cp = 0xA0;
                    } else if (name.size() >= 2 && name[0] == '#') {
                        bool hexForm = name[1] == 'x' || name[1] == 'X';
                        std::string_view digits = name.substr(hexForm ? 2 : 1);
                        uint32_t v = 0;
                        bool ok = !digits.empty() && digits.size() <= 8;
                        for (char d: digits) {
                            int dv = d >= '0' && d <= '9' ? d - '0'
                                     : hexForm && d >= 'a' && d <= 'f' ? d - 'a' + 10
                                     : hexForm && d >= 'A' && d <= 'F' ? d - 'A' + 10 : -1;
                            if (dv < 0) {
                                ok = false;
                                break;
                            }
                            v = v * (hexForm ? 16 : 10) + (uint32_t) dv;
                        }
                        if (ok && v > 0 && v <= 0x10FFFF && (v < 0xD800 || v > 0xDFFF)) {
                            cp = v;
                        }
                    }
                    if (cp != 0) {
                        appendUtf8(out, cp);
                        i = semi;
                        continue;
                    }
                }
                out += '&';   // not an entity this reader knows: kept as written
                continue;
            }
            if (latin1 && c >= 0x80) {
                uint32_t cp = c < 0xA0 ? CP1252[c - 0x80] : c;
                if (cp != 0) {
                    appendUtf8(out, cp);
                }
                continue;
            }
            out += (char) c;
        }
        return out;
    }

    bool Reader::attribute(std::string_view tag, std::string_view name, std::string &value) const {
        size_t i = 0;
        size_t n = tag.size();
        while (i < n && !isSpace(tag[i]) && tag[i] != '/') {
            i++;   // the element name
        }
        while (i < n) {
            while (i < n && (isSpace(tag[i]) || tag[i] == '/' || tag[i] == '?')) {
                i++;
            }
            size_t keyStart = i;
            while (i < n && tag[i] != '=' && !isSpace(tag[i])) {
                i++;
            }
            std::string_view key = tag.substr(keyStart, i - keyStart);
            while (i < n && isSpace(tag[i])) {
                i++;
            }
            if (i >= n || tag[i] != '=') {
                continue;
            }
            i++;
            while (i < n && isSpace(tag[i])) {
                i++;
            }
            std::string_view v;
            if (i < n && (tag[i] == '"' || tag[i] == '\'')) {
                char q = tag[i++];
                size_t close = tag.find(q, i);
                if (close == std::string_view::npos) {
                    close = n;
                }
                v = tag.substr(i, close - i);
                i = close < n ? close + 1 : n;
            } else {
                size_t start = i;
                while (i < n && !isSpace(tag[i])) {
                    i++;
                }
                v = tag.substr(start, i - start);
            }
            if (key == name) {
                value = decode(v, true);
                return true;
            }
        }
        return false;
    }

    void Reader::text(std::string_view raw, bool cdata) {
        if (!started) {
            if (raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) {
                raw.remove_prefix(3);
            }
            if (!trim(raw).empty()) {
                broken = true;   // text before the root element: not an XML document (an HTML page, JSON...)
            }
            return;
        }
        if (field == Field::None || fieldText.size() >= MAX_FIELD) {
            return;
        }
        std::string d = decode(raw, !cdata);
        fieldText.append(d, 0, MAX_FIELD - fieldText.size());
    }

    void Reader::startTag(std::string_view tag) {
        bool selfClosing = tag.back() == '/';
        size_t n = 0;
        while (n < tag.size() && !isSpace(tag[n]) && tag[n] != '/') {
            n++;
        }
        std::string_view name = tag.substr(0, n);
        if (name.empty()) {
            return;
        }
        if (!started) {
            started = true;
            if (name != "tv") {
                broken = true;   // another document type
                return;
            }
            rootTv = true;
        }
        std::string_view parent = stack.empty() ? std::string_view() : std::string_view(stack.back());
        std::string a;
        if (name == "channel" && parent == "tv") {
            inChannel = true;
            ch = Channel();
            attribute(tag, "id", ch.id);
            ch.id = std::string(trim(ch.id));
        } else if (name == "display-name" && inChannel && parent == "channel" && !selfClosing) {
            field = Field::DisplayName;
            fieldText.clear();
        } else if (name == "programme" && parent == "tv") {
            inProgramme = true;
            pr = Programme();
            titleLanguageMatched = false;
            descriptionLanguageMatched = false;
            st.programmes++;
            attribute(tag, "channel", pr.channel);
            pr.channel = std::string(trim(pr.channel));
            programmeValid = attribute(tag, "start", a) && parseTime(a, pr.start);
            if (!programmeValid) {
                st.badTimes++;
            }
            if (attribute(tag, "stop", a) && !parseTime(a, pr.stop)) {
                pr.stop = 0;   // unreadable: like none (the next programme's start ends it)
            }
        } else if ((name == "title" || name == "desc") && inProgramme && parent == "programme" && !selfClosing) {
            field = name == "title" ? Field::Title : Field::Description;
            fieldText.clear();
            fieldLanguage.clear();
            attribute(tag, "lang", fieldLanguage);
            fieldLanguage = lowerAscii(fieldLanguage);
        }
        if (selfClosing) {
            if (name == "channel" && inChannel) {
                endTag(name);   // <channel id="x"/>: a channel without names
            } else if (name == "programme" && inProgramme) {
                endTag(name);
            }
            return;
        }
        if (stack.size() < 32) {
            stack.emplace_back(name);
        } else {
            stack.emplace_back("");   // too deep for anything the guide reads
        }
    }

    void Reader::endTag(std::string_view name) {
        bool open = false;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (*it == name) {
                open = true;
                break;
            }
        }
        bool selfClosed = !open && ((name == "channel" && inChannel) || (name == "programme" && inProgramme));
        if (!open && !selfClosed) {
            return;   // a stray end tag
        }
        // elements left open inside it end with it
        while (open && !stack.empty() && stack.back() != name) {
            stack.pop_back();
        }
        if (open) {
            stack.pop_back();
        }
        if (name == "display-name" && field == Field::DisplayName) {
            std::string v(trim(fieldText));
            if (!v.empty() && ch.names.size() < 4) {
                ch.names.push_back(std::move(v));
            }
            field = Field::None;
        } else if (name == "title" && field == Field::Title) {
            bool match = languageMatches(fieldLanguage, language);
            if (trim(pr.title).empty() || (match && !titleLanguageMatched)) {
                pr.title = fieldText;
                titleLanguageMatched = match;
            }
            field = Field::None;
        } else if (name == "desc" && field == Field::Description) {
            bool match = languageMatches(fieldLanguage, language);
            if (trim(pr.description).empty() || (match && !descriptionLanguageMatched)) {
                pr.description = fieldText;
                descriptionLanguageMatched = match;
            }
            field = Field::None;
        } else if (name == "channel" && inChannel) {
            inChannel = false;
            field = Field::None;
            st.channels++;
            if (!ch.id.empty()) {
                handler.channel(ch);
            }
        } else if (name == "programme" && inProgramme) {
            inProgramme = false;
            field = Field::None;
            if (!programmeValid || pr.channel.empty()) {
                return;
            }
            if (trim(pr.title).empty()) {
                st.noTitle++;
                return;
            }
            handler.programme(pr);
        }
    }

    // ---------------------------------------------------------------------------------------------- Wanted

    Wanted Wanted::of(const iptv::LiveCatalog &live) {
        Wanted w;
        for (const auto &c: live.channels()) {
            w.add(c.epgId, c.name);
        }
        return w;
    }

    void Wanted::add(const std::string &tvgId, const std::string &name) {
        if (m3u::usableTvgId(tvgId)) {
            ids.insert(tvgId);
            lowerIds.insert(lowerAscii(tvgId));
        }
        std::string k = nameKey(name);
        if (!k.empty()) {
            names.insert(std::move(k));
        }
    }

    // ---------------------------------------------------------------------------------------------- Builder

    Builder::Builder(Wanted w, int64_t nowTime, BuildLimits l) : wanted(std::move(w)), now(nowTime), limits(l) {}

    bool Builder::wantedId(const std::string &id) const {
        return wanted.ids.count(id) > 0 || wanted.lowerIds.count(lowerAscii(id)) > 0;
    }

    void Builder::channel(Channel &c) {
        if (known.count(c.id) > 0) {
            return;   // described twice: the first description counts
        }
        bool want = wantedId(c.id);
        std::vector<std::string> keys;
        for (const auto &n: c.names) {
            std::string k = nameKey(n);
            if (k.empty()) {
                continue;
            }
            want = want || wanted.names.count(k) > 0;
            if (keys.size() < 3 && std::find(keys.begin(), keys.end(), k) == keys.end()) {
                keys.push_back(std::move(k));
            }
        }
        if (!want || entries.size() >= limits.maxChannels) {
            if (known.size() < limits.maxKnownChannels) {
                known.emplace(c.id, -1);
            }
            return;
        }
        known.emplace(c.id, (int) entries.size());
        entries.push_back({c.id, std::move(keys)});
        bs.matchedChannels++;
    }

    int Builder::channelOf(const std::string &id) {
        auto it = known.find(id);
        if (it != known.end()) {
            return it->second;
        }
        // a programme of a channel the guide did not describe: matched by its id only
        if (!wantedId(id) || entries.size() >= limits.maxChannels) {
            if (known.size() < limits.maxKnownChannels) {
                known.emplace(id, -1);
            }
            return -1;
        }
        known.emplace(id, (int) entries.size());
        entries.push_back({id, {}});
        bs.matchedChannels++;
        return (int) entries.size() - 1;
    }

    void Builder::programme(Programme &p) {
        int c = channelOf(p.channel);
        if (c < 0) {
            bs.unmatchedProgrammes++;
            return;
        }
        if ((p.stop > 0 && p.stop <= now) || p.start >= now + limits.keepAfter
            || (p.stop <= 0 && p.start + epg::MAX_DURATION <= now)) {
            bs.outsideWindow++;
            return;
        }
        std::string title = epg::cleanLine(p.title, epg::MAX_TITLE);
        if (title.empty()) {
            bs.dropped++;
            return;
        }
        std::string desc = epg::cleanLine(p.description, limits.maxDescription);
        if (records.size() >= limits.maxProgrammes || arena.size() + title.size() > limits.maxTextBytes) {
            bs.overLimit++;
            return;
        }
        if (arena.size() + title.size() + desc.size() > limits.maxTextBytes / 4 * 3) {
            bs.descriptionsDropped += desc.empty() ? 0 : 1;   // the last quarter is for titles only
            desc.clear();
        }
        Record r{};
        r.channel = (uint32_t) c;
        r.text = (uint32_t) arena.size();
        r.start = p.start;
        r.stop = p.stop;
        r.titleLength = (uint16_t) title.size();
        r.descriptionLength = (uint16_t) desc.size();
        arena += title;
        arena += desc;
        records.push_back(r);
    }

    bool Builder::write(const std::string &path, uint64_t key, std::string &error) {
        std::sort(records.begin(), records.end(), [](const Record &a, const Record &b) {
            return a.channel != b.channel ? a.channel < b.channel : a.start < b.start;
        });
        std::string tmp = path + ".tmp";
        FILE *f = fopen(tmp.c_str(), "wb");
        if (f == nullptr) {
            error = "cannot create the guide file";
            return false;
        }
        std::string header(HEADER_BYTES, '\0');
        bool ok = fwrite(header.data(), 1, header.size(), f) == header.size();
        uint64_t offset = HEADER_BYTES;
        std::string table;
        std::string block;
        struct Kept {
            int64_t start;
            int64_t stop;
            size_t record;
        };
        std::vector<Kept> kept;
        bs.kept = 0;
        bs.channelsWritten = 0;
        bs.coverageEnd = 0;
        for (size_t i = 0; i < records.size() && ok;) {
            size_t j = i;
            while (j < records.size() && records[j].channel == records[i].channel) {
                j++;
            }
            kept.clear();
            for (size_t k = i; k < j; k++) {
                const Record &r = records[k];
                int64_t stop = r.stop;
                if (stop <= 0) {
                    // no stop: the channel's next programme ends it (never a guessed duration; a duplicate of
                    // this one - the same slot within a minute - is not "the next programme")
                    size_t n = k + 1;
                    while (n < j && records[n].start < r.start + 60) {
                        n++;
                    }
                    if (n >= j || records[n].start - r.start > epg::MAX_DURATION) {
                        bs.dropped++;
                        continue;
                    }
                    stop = records[n].start;
                }
                if (stop <= r.start || stop - r.start > epg::MAX_DURATION) {
                    bs.dropped++;
                    continue;
                }
                if (!kept.empty()) {
                    Kept &last = kept.back();
                    if (r.start < last.start + 60) {
                        bs.dropped++;   // the same slot twice: the first one stays
                        continue;
                    }
                    if (r.start < last.stop) {
                        last.stop = r.start;   // runs into the next one: ends where the guide's next one starts
                    }
                }
                kept.push_back({r.start, stop, k});
            }
            size_t before = kept.size();
            kept.erase(std::remove_if(kept.begin(), kept.end(), [this](const Kept &k) { return k.stop <= now; }),
                       kept.end());
            bs.dropped += (int) (before - kept.size());
            if (kept.size() > limits.maxPerChannel) {
                bs.dropped += (int) (kept.size() - limits.maxPerChannel);
                kept.resize(limits.maxPerChannel);
            }
            const Entry &e = entries[records[i].channel];
            i = j;
            if (kept.empty()) {
                continue;
            }
            block.clear();
            for (const Kept &k: kept) {
                const Record &r = records[k.record];
                put(block, (uint64_t) k.start, 8);
                put(block, (uint64_t) (k.stop - k.start), 4);
                putText(block, arena.substr(r.text, r.titleLength), 0xFFFF);
                putText(block, arena.substr(r.text + r.titleLength, r.descriptionLength), 0xFFFF);
                bs.coverageEnd = std::max(bs.coverageEnd, k.stop);
            }
            ok = fwrite(block.data(), 1, block.size(), f) == block.size();
            putText(table, e.id, 1024);
            put(table, e.names.size(), 1);
            for (const auto &n: e.names) {
                putText(table, n, 1024);
            }
            put(table, offset, 8);
            put(table, block.size(), 4);
            put(table, kept.size(), 4);
            offset += block.size();
            bs.kept += (int) kept.size();
            bs.channelsWritten++;
        }
        if (ok) {
            ok = fwrite(table.data(), 1, table.size(), f) == table.size();
        }
        header.clear();
        header.append(MAGIC, sizeof(MAGIC));
        put(header, Index::VERSION, 4);
        put(header, (uint64_t) bs.channelsWritten, 4);
        put(header, (uint64_t) now, 8);
        put(header, key, 8);
        put(header, (uint64_t) bs.coverageEnd, 8);
        put(header, offset, 8);
        put(header, table.size(), 8);
        put(header, fnv(table), 8);
        ok = ok && fseek(f, 0, SEEK_SET) == 0 && fwrite(header.data(), 1, header.size(), f) == header.size();
        ok = fflush(f) == 0 && ok;
        ok = fclose(f) == 0 && ok;
        if (!ok) {
            remove(tmp.c_str());
            error = "cannot write the guide file";
            return false;
        }
#ifdef _WIN32
        remove(path.c_str());   // (the host tests: Windows' rename does not replace a file)
#endif
        if (rename(tmp.c_str(), path.c_str()) != 0) {
            remove(tmp.c_str());
            error = "cannot replace the guide file";
            return false;
        }
        records.clear();
        records.shrink_to_fit();
        arena.clear();
        arena.shrink_to_fit();
        return true;
    }

    // ---------------------------------------------------------------------------------------------- Index

    bool Index::open(const std::string &file, std::string &error) {
        path = file;
        header.clear();
        slots.clear();
        byId.clear();
        byLowerId.clear();
        byName.clear();
        FILE *f = fopen(file.c_str(), "rb");
        if (f == nullptr) {
            error = "no saved guide";
            return false;
        }
        header.assign(HEADER_BYTES, '\0');
        bool ok = fread(&header[0], 1, header.size(), f) == header.size() && header.compare(0, 8, MAGIC, 8) == 0;
        Cursor h{header, 8};
        uint32_t version = (uint32_t) h.get(4);
        uint32_t channels = (uint32_t) h.get(4);
        savedAt = (int64_t) h.get(8);
        sourceKey = h.get(8);
        coverageEnd = (int64_t) h.get(8);
        uint64_t tableOffset = h.get(8);
        uint64_t tableBytes = h.get(8);
        uint64_t sum = h.get(8);
        long size = -1;
        if (ok && fseek(f, 0, SEEK_END) == 0) {
            size = ftell(f);
        }
        ok = ok && version == VERSION && tableBytes <= MAX_TABLE_BYTES && tableOffset >= HEADER_BYTES
             && size >= 0 && tableOffset + tableBytes == (uint64_t) size;
        std::string table;
        if (ok) {
            table.resize((size_t) tableBytes);
            ok = fseek(f, (long) tableOffset, SEEK_SET) == 0
                 && fread(table.empty() ? nullptr : &table[0], 1, table.size(), f) == table.size() && fnv(table) == sum;
        }
        fclose(f);
        if (!ok) {
            error = version != VERSION ? "saved guide of another version" : "damaged saved guide";
            return false;
        }
        Cursor t{table};
        slots.reserve(channels);
        for (uint32_t i = 0; i < channels && t.ok; i++) {
            std::string id = t.text();
            int names = (int) t.get(1);
            std::vector<std::string> keys;
            for (int k = 0; k < names; k++) {
                keys.push_back(t.text());
            }
            Slot s;
            s.offset = t.get(8);
            s.bytes = (uint32_t) t.get(4);
            s.count = (uint32_t) t.get(4);
            if (!t.ok || s.offset < HEADER_BYTES || s.offset + s.bytes > tableOffset || s.bytes > MAX_BLOCK_BYTES) {
                t.ok = false;
                break;
            }
            int index = (int) slots.size();
            slots.push_back(s);
            byId.emplace(id, index);
            byLowerId.emplace(lowerAscii(id), index);
            for (auto &k: keys) {
                auto r = byName.emplace(std::move(k), index);
                if (!r.second && r.first->second != index) {
                    r.first->second = -1;   // two guide channels with this name: never matched by name
                }
            }
        }
        if (!t.ok || t.at != table.size()) {
            slots.clear();
            byId.clear();
            byLowerId.clear();
            byName.clear();
            error = "damaged saved guide";
            return false;
        }
        return true;
    }

    int Index::find(const iptv::LiveChannel &c) const {
        if (m3u::usableTvgId(c.epgId)) {
            auto it = byId.find(c.epgId);
            if (it != byId.end()) {
                return it->second;
            }
            it = byLowerId.find(lowerAscii(c.epgId));
            if (it != byLowerId.end()) {
                return it->second;
            }
        }
        auto it = byName.find(nameKey(c.name));
        return it != byName.end() && it->second >= 0 ? it->second : -1;
    }

    bool Index::read(int channel, int64_t now, std::vector<epg::Programme> &out, std::string &error, bool *gone) const {
        out.clear();
        if (gone) {
            *gone = false;
        }
        if (channel < 0 || channel >= (int) slots.size()) {
            error = "no such guide channel";
            return false;
        }
        const Slot &s = slots[(size_t) channel];
        FILE *f = fopen(path.c_str(), "rb");
        if (f == nullptr) {
            error = "saved guide missing";
            if (gone) {
                *gone = true;
            }
            return false;
        }
        std::string current(HEADER_BYTES, '\0');
        if (fread(&current[0], 1, current.size(), f) != current.size() || current != header) {
            fclose(f);
            error = "saved guide replaced";
            if (gone) {
                *gone = true;
            }
            return false;
        }
        std::string block(s.bytes, '\0');
        bool ok = fseek(f, (long) s.offset, SEEK_SET) == 0
                  && fread(block.empty() ? nullptr : &block[0], 1, block.size(), f) == block.size();
        fclose(f);
        if (!ok) {
            error = "saved guide unreadable";
            return false;
        }
        Cursor c{block};
        for (uint32_t i = 0; i < s.count && c.ok; i++) {
            epg::Programme p;
            p.start = (int64_t) c.get(8);
            p.stop = p.start + (int64_t) c.get(4);
            p.title = c.text();
            p.description = c.text();
            if (c.ok && p.stop > now && out.size() < epg::MAX_PROGRAMMES) {
                out.push_back(std::move(p));
            }
        }
        if (!c.ok) {
            out.clear();
            error = "damaged saved guide";
            return false;
        }
        return true;
    }
}
