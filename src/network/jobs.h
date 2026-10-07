// Small background job system: a fixed pool of worker threads, prioritized queue, cancellation,
// and completion callbacks delivered on the main thread (JobSystem::pump from the frame loop).
// Never create threads per request.

#ifndef PS4IPTV_NETWORK_JOBS_H
#define PS4IPTV_NETWORK_JOBS_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

enum class JobPriority {
    High = 0,     // user actions (login, opening a screen)
    Normal = 1,   // list refreshes
    Low = 2       // images, prefetch
};

class CancelToken {

public:

    CancelToken() : flag(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel() const { flag->store(true); }

    bool canceled() const { return flag->load(); }

    std::shared_ptr<std::atomic<bool>> shared() const { return flag; }

private:

    std::shared_ptr<std::atomic<bool>> flag;
};

class JobSystem {

public:

    using Work = std::function<void(const CancelToken &)>;
    using Done = std::function<void()>;

    JobSystem() = default;

    ~JobSystem();

    void start(int workers);

    void stop();

    // work runs on a worker; done runs on the main thread afterwards (skipped if canceled).
    CancelToken submit(JobPriority priority, const std::string &name, Work work, Done done);

    // Runs up to maxCallbacks completion callbacks; returns how many ran.
    int pump(int maxCallbacks = 16);

    int pending() const;

    // For tests: blocks until the queue is empty and all workers are idle.
    void waitIdle();

private:

    struct Job {
        JobPriority priority;
        std::string name;
        Work work;
        Done done;
        CancelToken token;
    };

    static void *threadMain(void *self);

    void workerLoop();

    mutable std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable idleCv;
    std::deque<Job> queues[3];
    std::deque<std::pair<CancelToken, Done>> completed;
    std::vector<void *> threads;  // pthread_t / std::thread*
    std::vector<CancelToken> runningTokens;
    int busy = 0;
    bool stopping = false;
};

#endif // PS4IPTV_NETWORK_JOBS_H
