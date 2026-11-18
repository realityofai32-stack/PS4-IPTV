// Logging and privacy (V7 Player): what a log line may show. Paths below a USB drive, a disc or the user's playlist
// folder keep only their root in error texts (fs::shownPath); and no log statement in the sources passes a password,
// a username, a MAC address, a token, a playlist / add-on address, a local path or a raw item id (a local file's id
// is its path) - each needs a redacting helper (m3u::displayUrl, protocols::origin, screens::hostOf, logId...).

#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/iptv/m3u.h"
#include "../../src/platform/fs.h"

TEST(privacy_shown_path_keeps_only_the_root) {
    CHECK_EQ(fs::shownPath("/usb0/Family Holiday/Kids.mkv"), std::string("/usb0/[...]"));
    CHECK_EQ(fs::shownPath("/usb7/a"), std::string("/usb7/[...]"));
    CHECK_EQ(fs::shownPath("/usb0"), std::string("/usb0"));
    CHECK_EQ(fs::shownPath("/usb0/"), std::string("/usb0/"));
    CHECK_EQ(fs::shownPath("/mnt/usb3/Movies/x.mkv"), std::string("/mnt/usb3/[...]"));
    CHECK_EQ(fs::shownPath("/disc/BDMV/STREAM/00001.m2ts"), std::string("/disc/[...]"));
    CHECK_EQ(fs::shownPath("/mnt/disc/video.mkv"), std::string("/mnt/disc/[...]"));
    CHECK_EQ(fs::shownPath("/data/PS4IPTV/playlists/My Secret List.m3u"), std::string("/data/PS4IPTV/playlists/[...]"));
    // the app's own data paths stay readable (diagnostics); look-alikes are not drives
    CHECK_EQ(fs::shownPath("/data/PS4IPTV/settings.json"), std::string("/data/PS4IPTV/settings.json"));
    CHECK_EQ(fs::shownPath("/usbx/a"), std::string("/usbx/a"));
    CHECK_EQ(fs::shownPath("/discography/a"), std::string("/discography/a"));
    // an error text of the file layer: errno and the root, not the user's folder or file name
    std::string text, err;
    CHECK(!fs::readFile("/usb7/Family Holiday/Kids Birthday.m3u", text, 1024, &err));
    CHECK(err.find("/usb7/[...]") != std::string::npos);
    CHECK(err.find("Family") == std::string::npos && err.find("Kids") == std::string::npos);
    CHECK(!fs::ensureDir("/usb7/Private Folder/Sub", &err));
    CHECK(err.find("Private") == std::string::npos);
    // addresses in the log: inner segments (credentials, an add-on's configuration) and local playlist names hidden
    CHECK_EQ(m3u::logUrl("/data/PS4IPTV/playlists/Family Favourites.m3u"), std::string("[local playlist]"));
    std::string addon = m3u::logUrl("https://addon.example.com/eyJ0b2tlbiI6ImV4YW1wbGV0b2tlbiJ9/manifest.json");
    CHECK(addon.find("eyJ0b2tlbiI6") == std::string::npos && addon.find("addon.example.com") == 0);
    std::string xt = m3u::logUrl("http://example.com:8080/get.php?username=exampleuser&password=examplepass&type=m3u");
    CHECK(xt.find("exampleuser") == std::string::npos && xt.find("examplepass") == std::string::npos);
}

namespace {
    struct Statement {
        std::string file;
        int line;
        std::string text;
    };

    // every LOG_I / LOG_W / LOG_E / LOG_V statement of the app's sources, comments and string contents kept
    std::vector<Statement> logStatements(const std::string &root) {
        std::vector<Statement> out;
        std::vector<std::string> dirs = {root};
        while (!dirs.empty()) {
            std::string d = dirs.back();
            dirs.pop_back();
            for (const auto &e: fs::listDir(d)) {
                std::string p = fs::join(d, e.name);
                if (e.dir) {
                    dirs.push_back(p);
                    continue;
                }
                if (p.size() < 4 || p.compare(p.size() - 4, 4, ".cpp") != 0) {
                    continue;
                }
                std::string s;
                fs::readFile(p, s, 4u << 20);
                for (size_t at = s.find("LOG_"); at != std::string::npos; at = s.find("LOG_", at + 4)) {
                    if (at + 6 > s.size() || std::string("IWEV").find(s[at + 4]) == std::string::npos || s[at + 5] != '(') {
                        continue;
                    }
                    size_t i = at + 6;
                    int depth = 1;
                    while (i < s.size() && depth > 0) {
                        char c = s[i];
                        if (c == '"') {
                            for (i++; i < s.size() && s[i] != '"'; i++) {
                                if (s[i] == '\\') {
                                    i++;
                                }
                            }
                        } else if (c == '(') {
                            depth++;
                        } else if (c == ')') {
                            depth--;
                        }
                        i++;
                    }
                    int line = 1;
                    for (size_t k = 0; k < at; k++) {
                        line += s[k] == '\n';
                    }
                    out.push_back({p.substr(root.size()), line, s.substr(at, i - at)});
                }
            }
        }
        return out;
    }
}

TEST(privacy_log_statements_never_pass_secrets) {
    const char *src = std::getenv("PS4IPTV_SOURCE_DIR");
    if (src == nullptr) {
        std::printf("  (skipped: PS4IPTV_SOURCE_DIR not set)\n");
        return;
    }
    std::vector<Statement> all = logStatements(src);
    CHECK(all.size() > 300);
    int bad = 0;
    for (const auto &st: all) {
        std::string why;
        for (const char *forbidden: {"password", ".username", "->username", ".mac", "->mac", "token.c_str()",
                                     "Token.c_str()", "playlistUrl.c_str()", "manifest.c_str()", "url.c_str()",
                                     "Url.c_str()", "item().id.c_str()", "it.id.c_str()", "n.id.c_str()",
                                     "->id.c_str()", "].id.c_str()", "displayUrl("}) {
            if (st.text.find(forbidden) != std::string::npos) {
                why = forbidden;
            }
        }
        // a path: only the log file's own (the app's data folder)
        if (st.text.find(".path.c_str()") != std::string::npos && st.text.find("fs.path.c_str()") == std::string::npos) {
            why = ".path.c_str()";
        }
        if (!why.empty()) {
            std::printf("  %s:%d passes %s: %s\n", st.file.c_str(), st.line, why.c_str(), st.text.substr(0, 160).c_str());
            bad++;
        }
    }
    CHECK_EQ(bad, 0);
}
