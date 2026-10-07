// Background DNS check of the stream hosts with the same call FFmpeg's tcp protocol makes
// (libavformat/tcp.c: getaddrinfo(hostname, port, {AF_UNSPEC, SOCK_STREAM})).
// Diagnostic only: playback still goes through mpv/FFmpeg. pPlay's local-file test did not
// exercise name resolution, so this tells DNS problems apart from HTTP/demux problems.

#ifndef PS4IPTV_TEST_NET_CHECK_H
#define PS4IPTV_TEST_NET_CHECK_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

struct DnsResult {
    std::string host;
    std::string port;
    bool done = false;
    bool ok = false;
    std::string addresses;
    std::string error;
    double ms = 0;
};

class DnsCheck {

public:

    // Starts resolving on a detached thread (getaddrinfo can block for the resolver timeout).
    void start(const std::vector<std::pair<std::string, std::string>> &hostPorts);

    std::vector<DnsResult> results() const;

private:

    struct State;
    std::shared_ptr<State> state;
};

#endif // PS4IPTV_TEST_NET_CHECK_H
