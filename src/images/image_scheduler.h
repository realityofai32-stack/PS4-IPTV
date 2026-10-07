// Which images to load next (host-testable, single-threaded: used from the main thread only).
//
// The screen calls want() with the URLs it needs in priority order (selected item, visible rows, a small
// prefetch window) every time the selection moves. want() replaces the queue, so images that scrolled
// away before their turn are never fetched. At most maxInFlight loads run at once; a URL is never queued
// or loaded twice. Failures are remembered: permanent ones (404, not an image) for the session,
// network errors until retryAfter seconds have passed.

#ifndef PS4IPTV_IMAGES_IMAGE_SCHEDULER_H
#define PS4IPTV_IMAGES_IMAGE_SCHEDULER_H

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace images {

    class Scheduler {

    public:

        struct Config {
            int maxInFlight = 2;
            size_t maxQueue = 40;
            double retryAfter = 120;   // seconds, after a network error
        };

        enum class State {
            Unknown,
            Queued,
            Loading,
            Ready,
            Failed
        };

        enum class Result {
            Ok,
            RetryLater,   // network error / timeout
            Permanent     // HTTP 4xx, not an image, decode failure
        };

        Scheduler();

        explicit Scheduler(Config config) : cfg(config) {}

        void want(const std::vector<std::string> &urls, double now);

        // the next URL to load (now Loading), or "" at the in-flight limit / when nothing is queued
        std::string next();

        void finished(const std::string &url, Result result, double now);

        State state(const std::string &url) const;

        // a Ready image left the memory cache: it may be requested again
        void forget(const std::string &url);

        // forget everything except loads in flight (cache cleared)
        void reset();

        int inFlight() const { return loading; }

        size_t queued() const { return queue.size(); }

    private:

        struct Item {
            State state = State::Unknown;
            double retryAt = 0;   // Failed: may be queued again after this time (< 0: never)
        };

        Config cfg;
        std::unordered_map<std::string, Item> items;
        std::deque<std::string> queue;
        int loading = 0;
    };
}

#endif // PS4IPTV_IMAGES_IMAGE_SCHEDULER_H
