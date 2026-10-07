#include <arpa/inet.h>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>

#include "diag_log.h"
#include "net_check.h"

struct DnsCheck::State {
    std::mutex mutex;
    std::vector<DnsResult> results;
};

namespace {
    double nowMs() {
        struct timeval tv{};
        gettimeofday(&tv, nullptr);
        return (double) tv.tv_sec * 1000.0 + (double) tv.tv_usec / 1000.0;
    }

    DnsResult resolve(const std::string &host, const std::string &port) {
        DnsResult r;
        r.host = host;
        r.port = port;
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;      // as libavformat/tcp.c
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *ai = nullptr;
        double t0 = nowMs();
        int ret = getaddrinfo(host.c_str(), port.c_str(), &hints, &ai);
        r.ms = nowMs() - t0;
        r.done = true;
        if (ret != 0) {
            r.error = diag::format("getaddrinfo error %d (%s)", ret, gai_strerror(ret));
            return r;
        }
        for (struct addrinfo *a = ai; a != nullptr; a = a->ai_next) {
            char buf[64] = {0};
            if (a->ai_family == AF_INET) {
                inet_ntop(AF_INET, &((struct sockaddr_in *) a->ai_addr)->sin_addr, buf, sizeof(buf));
            } else if (a->ai_family == AF_INET6) {
                inet_ntop(AF_INET6, &((struct sockaddr_in6 *) a->ai_addr)->sin6_addr, buf, sizeof(buf));
            }
            if (buf[0] && r.addresses.find(buf) == std::string::npos) {
                r.addresses += (r.addresses.empty() ? "" : " ") + std::string(buf);
            }
        }
        freeaddrinfo(ai);
        r.ok = true;
        return r;
    }
}

void DnsCheck::start(const std::vector<std::pair<std::string, std::string>> &hostPorts) {
    state = std::make_shared<State>();
    for (const auto &hp: hostPorts) {
        DnsResult pending;
        pending.host = hp.first;
        pending.port = hp.second;
        state->results.push_back(pending);
    }
    std::shared_ptr<State> s = state;
    std::thread([s, hostPorts]() {
        for (size_t i = 0; i < hostPorts.size(); i++) {
            DnsResult r = resolve(hostPorts[i].first, hostPorts[i].second);
            if (r.ok) {
                LOG_I("dns", "%s:%s -> %s (%.0f ms)", r.host.c_str(), r.port.c_str(), r.addresses.c_str(), r.ms);
            } else {
                LOG_E("dns", "%s:%s FAILED: %s (%.0f ms)", r.host.c_str(), r.port.c_str(), r.error.c_str(), r.ms);
            }
            std::lock_guard<std::mutex> lock(s->mutex);
            s->results[i] = r;
        }
    }).detach();
}

std::vector<DnsResult> DnsCheck::results() const {
    if (!state) {
        return {};
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->results;
}
