#include <atomic>
#include <chrono>
#include <thread>

#include "check.h"
#include "../../src/network/jobs.h"

TEST(jobs_callbacks_run_on_pumping_thread) {
    JobSystem jobs;
    jobs.start(2);
    std::atomic<int> worked{0};
    int done = 0;
    std::thread::id mainId = std::this_thread::get_id();
    bool doneOnMain = true;
    for (int i = 0; i < 20; i++) {
        jobs.submit(JobPriority::Normal, "t", [&](const CancelToken &) { worked++; },
                    [&] {
                        done++;
                        doneOnMain = doneOnMain && std::this_thread::get_id() == mainId;
                    });
    }
    jobs.waitIdle();
    while (jobs.pump(100) > 0) {}
    CHECK_EQ(worked.load(), 20);
    CHECK_EQ(done, 20);
    CHECK(doneOnMain);
    jobs.stop();
}

TEST(jobs_canceled_job_never_calls_back) {
    JobSystem jobs;
    jobs.start(1);
    std::atomic<bool> release{false};
    // occupy the single worker so the next job stays queued
    jobs.submit(JobPriority::High, "block", [&](const CancelToken &) {
        while (!release.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }, nullptr);
    bool ran = false;
    bool called = false;
    CancelToken t = jobs.submit(JobPriority::Normal, "x", [&](const CancelToken &) { ran = true; },
                                [&] { called = true; });
    t.cancel();
    release = true;
    jobs.waitIdle();
    jobs.pump(100);
    CHECK(!ran);
    CHECK(!called);
    jobs.stop();
}

TEST(jobs_priority_order) {
    JobSystem jobs;
    jobs.start(1);
    std::atomic<bool> release{false};
    jobs.submit(JobPriority::High, "block", [&](const CancelToken &) {
        while (!release.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }, nullptr);
    std::string order;
    std::mutex m;
    jobs.submit(JobPriority::Low, "low", [&](const CancelToken &) {
        std::lock_guard<std::mutex> l(m);
        order += "L";
    }, nullptr);
    jobs.submit(JobPriority::High, "high", [&](const CancelToken &) {
        std::lock_guard<std::mutex> l(m);
        order += "H";
    }, nullptr);
    jobs.submit(JobPriority::Normal, "normal", [&](const CancelToken &) {
        std::lock_guard<std::mutex> l(m);
        order += "N";
    }, nullptr);
    release = true;
    jobs.waitIdle();
    CHECK_EQ(order, std::string("HNL"));
    jobs.stop();
}

TEST(jobs_stop_cancels_running_work) {
    JobSystem jobs;
    jobs.start(1);
    std::atomic<bool> started{false};
    std::atomic<bool> sawCancel{false};
    jobs.submit(JobPriority::High, "long", [&](const CancelToken &token) {
        started = true;
        for (int i = 0; i < 5000 && !token.canceled(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        sawCancel = token.canceled();
    }, nullptr);
    while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto t0 = std::chrono::steady_clock::now();
    jobs.stop();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(sawCancel.load());
    CHECK(ms < 1000);
}
