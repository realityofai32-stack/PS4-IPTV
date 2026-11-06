// The HTTP client's scheme, redirect and retry rules (network/http_policy.h). The client itself is exercised against
// failing / redirecting local servers by tests/network/run-network-tests.sh.

#include "check.h"
#include "../../src/network/http_policy.h"

using namespace httppolicy;

TEST(http_only_http_and_https_are_requested) {
    CHECK(schemeAllowed("http://example.com/a"));
    CHECK(schemeAllowed("HTTPS://example.com"));
    for (const char *u: {"file:///data/PS4IPTV/profiles.json", "gopher://192.168.1.1:80/_POST", "dict://127.0.0.1:6379/",
                         "ftp://example.com/x", "tftp://10.0.0.1/x", "smb://nas/share", "ldap://x", "telnet://x",
                         "javascript:alert(1)", "/data/PS4IPTV/x", "", "http:/x", "httpx://x", " http://x"}) {
        CHECK(!schemeAllowed(u));
    }
}

TEST(http_origin_parsing) {
    Origin o;
    CHECK(parseOrigin("HTTP://User:Pass@Example.COM:8080/p?q", o));
    CHECK_EQ(o.scheme, std::string("http"));
    CHECK_EQ(o.host, std::string("example.com"));
    CHECK_EQ(o.port, 8080);
    CHECK(parseOrigin("https://example.com", o) && o.port == 443);
    CHECK(parseOrigin("http://[::1]:81/x", o) && o.host == "[::1]" && o.port == 81);
    CHECK(parseOrigin("http://example.com?x=1", o) && o.host == "example.com" && o.port == 80);
    CHECK(!parseOrigin("http://:80/", o));
    CHECK(!parseOrigin("http://example.com:99999/", o));
    CHECK(!parseOrigin("http://example.com:8a/", o));
    CHECK(!parseOrigin("http://[::1/", o));
    CHECK(!parseOrigin("example.com", o));
}

TEST(http_redirect_policy) {
    // ordinary redirects keep working (http -> https upgrade, another path, another host without own headers)
    CHECK(checkRedirect("http://a.example.com/x", "https://a.example.com/y", true, false) == Redirect::Follow);
    CHECK(checkRedirect("http://a.example.com/x", "http://b.example.com/y", true, false) == Redirect::Follow);
    CHECK(checkRedirect("https://a.example.com/x", "https://cdn.example.net/y", true, false) == Redirect::Follow);
    // never another scheme
    CHECK(checkRedirect("http://a.example.com/x", "file:///etc/passwd", true, false) == Redirect::BadScheme);
    CHECK(checkRedirect("http://a.example.com/x", "gopher://10.0.0.1:25/_HELO", false, false) == Redirect::BadScheme);
    CHECK(checkRedirect("http://a.example.com/x", "not a url", false, false) == Redirect::Invalid);
    // https -> http: refused when the request carries credentials, allowed for artwork
    CHECK(checkRedirect("https://a.example.com/api?password=x", "http://a.example.com/api", true, false) == Redirect::Downgrade);
    CHECK(checkRedirect("https://img.example.com/p.png", "http://img2.example.com/p.png", false, false) == Redirect::Follow);
    // a portal request (own Cookie / Authorization header lines) stays on its host (any port, upgrade allowed)
    CHECK(checkRedirect("http://portal.example.com/c/", "http://evil.example.net/c/", true, true) == Redirect::OtherHost);
    CHECK(checkRedirect("http://portal.example.com/c/", "http://PORTAL.example.com:8080/stalker_portal/", true, true)
          == Redirect::Follow);
    CHECK(checkRedirect("http://portal.example.com/c/", "https://portal.example.com/c/", true, true) == Redirect::Follow);
    CHECK(checkRedirect("http://portal.example.com/", "http://user@evil.example.net/", true, true) == Redirect::OtherHost);
    CHECK(checkRedirect("http://portal.example.com/c/", "http://www.portal.example.com/c/", true, true) == Redirect::Follow);
    CHECK(checkRedirect("http://www.portal.example.com/c/", "http://portal.example.com/c/", true, true) == Redirect::Follow);
    CHECK(checkRedirect("http://portal.example.com/c/", "http://www.evil.example.com/c/", true, true) == Redirect::OtherHost);
    CHECK(checkRedirect("http://example.com/c/", "http://wwwexample.com/c/", true, true) == Redirect::OtherHost);
}

TEST(http_retry_policy_is_bounded_and_only_for_quick_temporary_failures) {
    // temporary and quick: retried, at most MAX_RETRIES times
    CHECK(shouldRetry(Failure::Connect, 0, 0.1, 30000, 0, 2, false));
    CHECK(shouldRetry(Failure::Interrupted, 0, 1.0, 30000, 1, 2, false));
    CHECK(shouldRetry(Failure::Dns, 0, 0.2, 30000, 0, 2, false));
    CHECK(shouldRetry(Failure::None, 503, 0.3, 30000, 0, 2, false));
    CHECK(shouldRetry(Failure::None, 502, 0.3, 30000, 0, 2, false));
    CHECK(shouldRetry(Failure::None, 504, 0.3, 30000, 0, 2, false));
    CHECK(!shouldRetry(Failure::Connect, 0, 0.1, 30000, 2, 2, false));
    CHECK(!shouldRetry(Failure::Connect, 0, 0.1, 30000, 0, 0, false));
    // never: canceled, success, client errors, rate limits, TLS, certificate, too large, refused redirect, timeouts
    CHECK(!shouldRetry(Failure::Connect, 0, 0.1, 30000, 0, 2, true));
    for (long status: {200L, 204L, 301L, 400L, 401L, 403L, 404L, 429L, 500L}) {
        CHECK(!shouldRetry(Failure::None, status, 0.1, 30000, 0, 2, false));
    }
    for (Failure f: {Failure::Canceled, Failure::Timeout, Failure::Tls, Failure::TlsCertificate, Failure::TooLarge,
                     Failure::Redirect, Failure::Other}) {
        CHECK(!shouldRetry(f, 0, 0.1, 30000, 0, 2, false));
    }
    // a failure after half the time budget (a slow server) is not repeated
    CHECK(!shouldRetry(Failure::Interrupted, 0, 16.0, 30000, 0, 2, false));
    CHECK(shouldRetry(Failure::Interrupted, 0, 14.0, 30000, 0, 2, false));
    // growing pauses, bounded
    CHECK(backoffSeconds(1) > 0.4 && backoffSeconds(1) < backoffSeconds(2) && backoffSeconds(2) <= 3.0);
    CHECK(MAX_RETRIES <= 2 && MAX_REDIRECTS <= 10);
}
