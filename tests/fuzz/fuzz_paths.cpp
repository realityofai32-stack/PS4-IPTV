// libFuzzer target: Local Media path handling (normalization, root containment, names, extensions, ordering,
// formats) and the player's protocol / TLS classification. Invariant: nothing containing ".." ever normalizes, and
// whatever is "within" a root starts with that root.
#include <cstdint>
#include <string>

#include "../../src/media/local_media.h"
#include "../../src/player/protocols.h"

#define REQUIRE(c) do { if (!(c)) { __builtin_trap(); } } while (0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string in((const char *) data, size);
    std::string out;
    if (localmedia::normalize(in, out)) {
        REQUIRE(!out.empty() && out[0] == '/');
        REQUIRE(out.find("//") == std::string::npos && out.find("/./") == std::string::npos);
        REQUIRE(out.find("/../") == std::string::npos && (out.size() < 3 || out.compare(out.size() - 3, 3, "/..") != 0));
        std::string again;
        REQUIRE(localmedia::normalize(out, again) && again == out);   // idempotent
    }
    for (const auto &r: localmedia::candidateRoots()) {
        if (localmedia::within(r, in)) {
            std::string root, p;
            REQUIRE(localmedia::normalize(r.path, root) && localmedia::normalize(in, p));
            REQUIRE(p.compare(0, root.size(), root) == 0 && (p.size() == root.size() || p[root.size()] == '/'));
        }
    }
    if (localmedia::safeName(in)) {
        REQUIRE(in.find('/') == std::string::npos && in != ".." && in != ".");
    }
    localmedia::extensionOf(in);
    size_t half = size / 2;
    std::string a = in.substr(0, half), b = in.substr(half);
    REQUIRE(!(localmedia::naturalLess(a, b) && localmedia::naturalLess(b, a)));   // a strict order
    REQUIRE(!localmedia::naturalLess(a, a));
    localmedia::Formats f;
    f.addDemuxer(a, b);
    f.supported(in);
    protocols::scheme(in);
    protocols::check(in, true);
    protocols::shownScheme(in);
    protocols::classifyTls(in);
    std::string origin = protocols::origin(in);
    REQUIRE(origin.find('@') == std::string::npos || protocols::scheme(in).empty());
    return 0;
}
