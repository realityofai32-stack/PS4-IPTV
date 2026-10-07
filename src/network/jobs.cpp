#include "jobs.h"
#include "../platform/log.h"

#ifdef __PS4__
#include <pthread.h>
#else
#include <thread>
#endif

namespace {
    // libcurl + mbedTLS handshakes and JSON parsing need more than musl's small default thread stack
    const size_t WORKER_STACK_BYTES = 1024 * 1024;
}

JobSystem::~JobSystem() {
    stop();
}

void *JobSystem::threadMain(void *self) {
    ((JobSystem *) self)->workerLoop();
    return nullptr;
}

void JobSystem::start(int workers) {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = false;
    for (int i = 0; i < workers; i++) {
#ifdef __PS4__
        auto *t = new pthread_t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, WORKER_STACK_BYTES);
        int rc = pthread_create(t, &attr, &JobSystem::threadMain, this);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            LOG_E("jobs", "pthread_create for worker %d failed: %d", i, rc);
            delete t;
            continue;
        }
        threads.push_back(t);
#else
        threads.push_back(new std::thread(&JobSystem::threadMain, this));
#endif
    }
    LOG_I("jobs", "%d worker thread(s) started (stack %zu KiB)", (int) threads.size(), WORKER_STACK_BYTES / 1024);
}

void JobSystem::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (threads.empty()) {
            return;
        }
        stopping = true;
        for (auto &t: runningTokens) {
            t.cancel();  // aborts in-flight transfers (curl progress callback)
        }
        for (auto &q: queues) {
            for (auto &j: q) {
                j.token.cancel();
            }
            q.clear();
        }
    }
    cv.notify_all();
    for (void *t: threads) {
#ifdef __PS4__
        pthread_join(*(pthread_t *) t, nullptr);
        delete (pthread_t *) t;
#else
        ((std::thread *) t)->join();
        delete (std::thread *) t;
#endif
    }
    threads.clear();
    std::lock_guard<std::mutex> lock(mutex);
    completed.clear();
}

CancelToken JobSystem::submit(JobPriority priority, const std::string &name, Work work, Done done) {
    Job job{priority, name, std::move(work), std::move(done), CancelToken()};
    CancelToken token = job.token;
    {
        std::lock_guard<std::mutex> lock(mutex);
        queues[(int) priority].push_back(std::move(job));
    }
    cv.notify_one();
    return token;
}

void JobSystem::workerLoop() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] {
                return stopping || !queues[0].empty() || !queues[1].empty() || !queues[2].empty();
            });
            if (stopping) {
                return;
            }
            for (auto &q: queues) {
                if (!q.empty()) {
                    job = std::move(q.front());
                    q.pop_front();
                    break;
                }
            }
            busy++;
            runningTokens.push_back(job.token);
        }
        if (!job.token.canceled()) {
            job.work(job.token);
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            busy--;
            for (auto it = runningTokens.begin(); it != runningTokens.end(); ++it) {
                if (it->shared() == job.token.shared()) {
                    runningTokens.erase(it);
                    break;
                }
            }
            if (job.done && !job.token.canceled()) {
                completed.emplace_back(job.token, std::move(job.done));
            }
        }
        idleCv.notify_all();
    }
}

int JobSystem::pump(int maxCallbacks) {
    int ran = 0;
    while (ran < maxCallbacks) {
        std::pair<CancelToken, Done> item;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (completed.empty()) {
                break;
            }
            item = std::move(completed.front());
            completed.pop_front();
        }
        if (!item.first.canceled() && item.second) {
            item.second();
        }
        ran++;
    }
    return ran;
}

int JobSystem::pending() const {
    std::lock_guard<std::mutex> lock(mutex);
    return (int) (queues[0].size() + queues[1].size() + queues[2].size() + completed.size()) + busy;
}

void JobSystem::waitIdle() {
    std::unique_lock<std::mutex> lock(mutex);
    idleCv.wait(lock, [this] { return busy == 0 && queues[0].empty() && queues[1].empty() && queues[2].empty(); });
}
