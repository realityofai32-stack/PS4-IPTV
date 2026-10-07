#include <algorithm>
#include <cctype>

#include "test_env.h"

namespace {

    std::string trim(const std::string &s) {
        size_t b = 0;
        size_t e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) {
            b++;
        }
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) {
            e--;
        }
        return s.substr(b, e - b);
    }

    std::string upper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::toupper(c); });
        return s;
    }

}

StreamConfig parseStreamConfig(const std::string &content, const std::string &path) {
    StreamConfig cfg;
    cfg.found = true;
    cfg.path = path;

    std::string text = content;
    if (text.size() >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB
        && (unsigned char) text[2] == 0xBF) {
        text = text.substr(3);  // UTF-8 BOM (Notepad)
    }

    size_t start = 0;
    int lineNo = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        std::string line = trim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        start = end == std::string::npos ? text.size() + 1 : end + 1;
        lineNo++;

        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            cfg.problems.push_back("line " + std::to_string(lineNo) + ": no '=' (ignored)");
            continue;
        }
        std::string key = upper(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"')
                                  || (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "TS") {
            cfg.tsUrl = value;
        } else if (key == "HLS") {
            cfg.hlsUrl = value;
        } else if (key == "LOCAL") {
            cfg.localPath = value;
        } else if (key == "USER_AGENT") {
            cfg.userAgent = value;
        } else if (key == "MPV_OPT") {
            size_t oeq = value.find('=');
            if (oeq == std::string::npos || oeq == 0) {
                cfg.problems.push_back("line " + std::to_string(lineNo) + ": MPV_OPT needs name=value (ignored)");
            } else {
                cfg.mpvOptions.emplace_back(trim(value.substr(0, oeq)), trim(value.substr(oeq + 1)));
            }
        } else {
            // the key name is safe to show, the value is not
            cfg.problems.push_back("line " + std::to_string(lineNo) + ": unknown key '" + key + "' (ignored)");
        }
    }

    for (const auto *entry: {&cfg.tsUrl, &cfg.hlsUrl}) {
        if (!entry->empty() && entry->find("://") == std::string::npos) {
            cfg.problems.push_back(std::string(entry == &cfg.tsUrl ? "TS" : "HLS") + " value is not a URL");
        }
        if (entry->find('<') != std::string::npos) {
            cfg.problems.push_back(std::string(entry == &cfg.tsUrl ? "TS" : "HLS")
                                   + " still contains the <placeholder> from the example file");
        }
    }
    return cfg;
}
