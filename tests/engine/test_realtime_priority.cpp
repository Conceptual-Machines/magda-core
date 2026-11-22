// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>

#include "exec/RealtimePriority.hpp"

#if defined(__linux__)
    #include <sched.h>
#endif

/// @file What realtime::raise() grants is what the thread then runs at, on any machine.

using namespace magda::engine;

#if defined(__linux__)

TEST_CASE("A thread raised by tid runs at the priority it was granted", "[engine][realtime]") {
    realtime::Grant grant;
    int policy = -1;
    sched_param param{};

    std::atomic<std::int64_t> threadId{0};
    std::atomic<bool> raised{false};
    std::thread thread([&] {
        threadId = realtime::currentThreadId();
        while (!raised.load())
            std::this_thread::yield();
        policy = sched_getscheduler(0) & ~SCHED_RESET_ON_FORK;
        sched_getparam(0, &param);
    });
    while (threadId.load() == 0)
        std::this_thread::yield();

    grant = realtime::raise(threadId, 40);
    raised = true;
    thread.join();

    // A CI runner may grant nothing; then the thread must be left as it was.
    if (grant.priority > 0) {
        CHECK(policy == SCHED_RR);
        CHECK(param.sched_priority == grant.priority);
        CHECK(grant.priority <= 40);
    } else {
        CHECK(policy == SCHED_OTHER);
        CHECK_FALSE(grant.viaRtkit);
        CHECK(realtime::anyThreadRefused());
    }
    CHECK_FALSE(realtime::describe(grant).isEmpty());
}

TEST_CASE("A time-shared thread is not reported as real-time", "[engine][realtime]") {
    bool realtimeAtStart = true;
    std::thread([&] { realtimeAtStart = realtime::currentThreadIsRealtime(); }).join();
    CHECK_FALSE(realtimeAtStart);
}

#endif
