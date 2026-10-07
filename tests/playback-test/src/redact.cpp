#include <algorithm>
#include <mutex>
#include <vector>

#include "redact.h"

namespace {

    const char *const MASK = "[REDACTED]";

    // Path markers after which Xtream Codes puts "<username>/<password>/".
    const char *const XTREAM_MARKERS[] = {"/live/", "/movie/", "/series/", "/timeshift/"};

    // Query parameters whose values are treated as credentials.
    const char *const QUERY_KEYS[] = {"username=", "password=", "user=", "pass=", "token=", "auth="};

    std::mutex g_mutex;
    std::vector<std::string> g_secrets;  // sorted longest first

    std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        return s;
    }

    void replaceAll(std::string &text, const std::string &what, const std::string &with) {
        if (what.empty()) {
            return;
        }
        size_t pos = 0;
        while ((pos = text.find(what, pos)) != std::string::npos) {
            text.replace(pos, what.size(), with);
            pos += with.size();
        }
    }

    bool isUrlStop(char c) {
        return c == '/' || c == '?' || c == '#' || c == '&' || c == '\'' || c == '"'
               || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ')' || c == ']' || c == '>';
    }

    bool maskedAt(const std::string &text, size_t pos) {
        return text.compare(pos, std::string(MASK).size(), MASK) == 0;
    }

    // Replaces the next `count` path segments following each marker.
    void maskSegmentsAfter(std::string &text, const char *marker, int count) {
        const std::string m = marker;
        size_t pos = 0;
        while ((pos = text.find(m, pos)) != std::string::npos) {
            size_t seg = pos + m.size();
            for (int i = 0; i < count && seg < text.size(); i++) {
                size_t end;
                if (maskedAt(text, seg)) {
                    end = seg + std::string(MASK).size();
                } else {
                    end = seg;
                    while (end < text.size() && !isUrlStop(text[end])) {
                        end++;
                    }
                    if (end == seg) {
                        break;
                    }
                    text.replace(seg, end - seg, MASK);
                    end = seg + std::string(MASK).size();
                }
                if (end >= text.size() || text[end] != '/') {
                    break;
                }
                seg = end + 1;
            }
            pos += m.size();
        }
    }

    void maskQueryValues(std::string &text) {
        std::string lower = toLower(text);
        for (const char *key: QUERY_KEYS) {
            const std::string k = key;
            size_t pos = 0;
            while ((pos = lower.find(k, pos)) != std::string::npos) {
                // only match at a parameter boundary
                if (pos > 0 && lower[pos - 1] != '?' && lower[pos - 1] != '&') {
                    pos += k.size();
                    continue;
                }
                size_t start = pos + k.size();
                if (maskedAt(text, start)) {
                    pos = start;
                    continue;
                }
                size_t end = start;
                while (end < text.size() && text[end] != '&' && !isUrlStop(text[end])) {
                    end++;
                }
                if (end > start) {
                    text.replace(start, end - start, MASK);
                    lower.replace(start, end - start, MASK);
                }
                pos = start;
            }
        }
    }

    // "scheme://user:pass@host" -> "scheme://[REDACTED]@host"
    void maskUserInfo(std::string &text) {
        size_t pos = 0;
        while ((pos = text.find("://", pos)) != std::string::npos) {
            size_t start = pos + 3;
            size_t end = start;
            while (end < text.size() && !isUrlStop(text[end]) && text[end] != '@') {
                end++;
            }
            if (end < text.size() && text[end] == '@' && end > start) {
                text.replace(start, end - start, MASK);
            }
            pos = start;
        }
    }

    void addSecretLocked(const std::string &s) {
        // very short values would mangle unrelated text; the structural rules still cover them
        if (s.size() < 3 || s == MASK) {
            return;
        }
        if (std::find(g_secrets.begin(), g_secrets.end(), s) == g_secrets.end()) {
            g_secrets.push_back(s);
            std::sort(g_secrets.begin(), g_secrets.end(),
                      [](const std::string &a, const std::string &b) { return a.size() > b.size(); });
        }
    }

    std::string pathSegment(const std::string &path, size_t index) {
        size_t start = 0;
        for (size_t i = 0; i <= index; i++) {
            size_t end = path.find('/', start);
            if (i == index) {
                return path.substr(start, end == std::string::npos ? std::string::npos : end - start);
            }
            if (end == std::string::npos) {
                return {};
            }
            start = end + 1;
        }
        return {};
    }
}

namespace redact {

    void addUrl(const std::string &url) {
        const std::string lower = toLower(url);
        std::lock_guard<std::mutex> lock(g_mutex);

        // userinfo
        size_t scheme = url.find("://");
        if (scheme != std::string::npos) {
            size_t hostStart = scheme + 3;
            size_t pathStart = url.find('/', hostStart);
            size_t at = url.find('@', hostStart);
            if (at != std::string::npos && (pathStart == std::string::npos || at < pathStart)) {
                std::string userInfo = url.substr(hostStart, at - hostStart);
                size_t colon = userInfo.find(':');
                addSecretLocked(userInfo.substr(0, colon));
                if (colon != std::string::npos) {
                    addSecretLocked(userInfo.substr(colon + 1));
                }
            }
        }

        // Xtream path credentials
        for (const char *marker: XTREAM_MARKERS) {
            size_t pos = lower.find(marker);
            if (pos != std::string::npos) {
                std::string rest = url.substr(pos + std::string(marker).size());
                addSecretLocked(pathSegment(rest, 0));
                addSecretLocked(pathSegment(rest, 1));
            }
        }

        // query values
        size_t q = url.find('?');
        if (q != std::string::npos) {
            std::string query = url.substr(q + 1);
            std::string lq = toLower(query);
            for (const char *key: QUERY_KEYS) {
                size_t pos = lq.find(key);
                if (pos != std::string::npos && (pos == 0 || lq[pos - 1] == '&')) {
                    size_t start = pos + std::string(key).size();
                    size_t end = query.find('&', start);
                    addSecretLocked(query.substr(start, end == std::string::npos ? std::string::npos : end - start));
                }
            }
        }
    }

    void clearSecrets() {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_secrets.clear();
    }

    std::string apply(const std::string &text) {
        std::string out = text;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (const auto &secret: g_secrets) {
                replaceAll(out, secret, MASK);
            }
        }
        for (const char *marker: XTREAM_MARKERS) {
            maskSegmentsAfter(out, marker, 2);
        }
        maskQueryValues(out);
        maskUserInfo(out);
        return out;
    }

    UrlInfo parseUrl(const std::string &url) {
        UrlInfo info;
        size_t scheme = url.find("://");
        if (scheme == std::string::npos || scheme == 0) {
            return info;
        }
        info.scheme = toLower(url.substr(0, scheme));

        size_t hostStart = scheme + 3;
        size_t pathStart = url.find_first_of("/?#", hostStart);
        std::string authority = url.substr(hostStart, pathStart == std::string::npos ? std::string::npos
                                                                                     : pathStart - hostStart);
        size_t at = authority.rfind('@');
        if (at != std::string::npos) {
            authority = authority.substr(at + 1);
        }
        size_t colon = authority.rfind(':');
        if (colon != std::string::npos && authority.find(']') == std::string::npos) {
            info.host = authority.substr(0, colon);
            info.port = authority.substr(colon + 1);
        } else {
            info.host = authority;
        }
        if (info.host.empty()) {
            return info;
        }

        std::string path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
        size_t query = path.find_first_of("?#");
        bool hadQuery = query != std::string::npos;
        if (hadQuery) {
            path = path.substr(0, query);
        }
        size_t lastSlash = path.rfind('/');
        std::string last = lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);
        size_t dot = last.rfind('.');
        if (dot != std::string::npos) {
            info.extension = toLower(last.substr(dot + 1));
        }

        info.sanitized = apply(info.scheme + "://" + authority + path) + (hadQuery ? "?[QUERY REDACTED]" : "");
        info.valid = true;
        return info;
    }
}
