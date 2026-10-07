#include "image_scheduler.h"

namespace images {

    Scheduler::Scheduler() : cfg(Config()) {}

    void Scheduler::want(const std::vector<std::string> &urls, double now) {
        // drop the previous queue: what scrolled away is no longer wanted
        for (const auto &u: queue) {
            auto it = items.find(u);
            if (it != items.end() && it->second.state == State::Queued) {
                items.erase(it);
            }
        }
        queue.clear();
        for (const auto &u: urls) {
            if (u.empty() || queue.size() >= cfg.maxQueue) {
                continue;
            }
            Item &item = items[u];
            if (item.state == State::Queued || item.state == State::Loading || item.state == State::Ready) {
                continue;   // duplicate in this list, in flight, or done
            }
            if (item.state == State::Failed && (item.retryAt < 0 || now < item.retryAt)) {
                continue;
            }
            item.state = State::Queued;
            queue.push_back(u);
        }
    }

    std::string Scheduler::next() {
        if (loading >= cfg.maxInFlight || queue.empty()) {
            return "";
        }
        std::string u = queue.front();
        queue.pop_front();
        items[u].state = State::Loading;
        loading++;
        return u;
    }

    void Scheduler::finished(const std::string &url, Result result, double now) {
        auto it = items.find(url);
        if (it == items.end() || it->second.state != State::Loading) {
            return;
        }
        loading--;
        if (result == Result::Ok) {
            it->second.state = State::Ready;
        } else {
            it->second.state = State::Failed;
            it->second.retryAt = result == Result::Permanent ? -1 : now + cfg.retryAfter;
        }
    }

    Scheduler::State Scheduler::state(const std::string &url) const {
        auto it = items.find(url);
        return it == items.end() ? State::Unknown : it->second.state;
    }

    void Scheduler::forget(const std::string &url) {
        auto it = items.find(url);
        if (it != items.end() && it->second.state == State::Ready) {
            items.erase(it);
        }
    }

    void Scheduler::reset() {
        queue.clear();
        for (auto it = items.begin(); it != items.end();) {
            if (it->second.state == State::Loading) {
                ++it;
            } else {
                it = items.erase(it);
            }
        }
    }
}
