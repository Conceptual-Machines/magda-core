#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "magda/daw/audio/plugins/engine/ControlExecutor.hpp"

/**
 * @file test_control_executor.cpp
 * @brief The one thread a device's control operations run on (#2270).
 *
 * What the endpoint above it rests on, and the reason it is a type rather than
 * a convention: reading a plugin's state, loading a preset and opening an
 * editor each suspend the plugin and resume it, so two of them overlapping
 * means the first to finish resumes a plugin the second is still inside. A host
 * with a window has the message thread to answer that. A headless one has
 * whatever threads it has, which is exactly why it needs an executor of its own
 * rather than permission for all of them.
 *
 * The message-thread implementation is not tested here, and deliberately: it is
 * two calls into juce::MessageManager, and this target has no message thread to
 * put it on. What is tested is the one that had to be written.
 */

namespace adapter = magda::daw::audio::engine_adapter;

using adapter::ExecutionState;

TEST_CASE("A serial executor runs its work on one thread, in order", "[engine][control]") {
    adapter::SerialControlThread executor;

    std::vector<int> order;
    std::vector<std::thread::id> threads;

    // Queued from several threads at once, which is the case a headless host
    // presents: nothing about who asks says anything about who runs it.
    std::vector<std::thread> askers;
    for (int index = 0; index < 4; ++index)
        askers.emplace_back([&executor, &order, &threads, index] {
            for (int item = 0; item < 8; ++item)
                CHECK(executor.run([&order, &threads, index](ExecutionState state) {
                    if (state != ExecutionState::Ran)
                        return;

                    // No lock: the claim is that this only ever runs on one
                    // thread, so a second one writing here would be the finding
                    // rather than a reason to synchronise.
                    order.push_back(index);
                    threads.push_back(std::this_thread::get_id());
                }));
        });

    for (auto& asker : askers)
        asker.join();

    // Drained by asking for one more thing and waiting for it: work runs in the
    // order it was accepted, so anything queued before this has run by the time
    // this has.
    std::promise<void> drained;
    auto emptied = drained.get_future();
    REQUIRE(executor.run([&drained](ExecutionState) { drained.set_value(); }));
    emptied.wait();

    REQUIRE(order.size() == 32);
    REQUIRE(threads.size() == 32);

    for (const auto& thread : threads)
        CHECK(thread == threads.front());

    // Each asker's own items all arrived. Interleaving between askers is fine
    // and losing one is not.
    for (int index = 0; index < 4; ++index) {
        auto seen = 0;
        for (const auto item : order)
            if (item == index)
                ++seen;

        CHECK(seen == 8);
    }
}

TEST_CASE("Work asked for from the executor is queued behind what asked for it",
          "[engine][control]") {
    // The half that makes "one at a time" mean anything. An operation that has
    // suspended a plugin and asks for another must not have the second run
    // inside it: that second one would resume the plugin and let audio back in
    // halfway through the first, which is the failure this executor exists to
    // remove rather than to relocate.
    adapter::SerialControlThread executor;

    std::atomic<bool> nested{false};
    std::atomic<bool> nestedRanInside{false};
    std::promise<void> outerDone;
    auto outerFinished = outerDone.get_future();

    REQUIRE(executor.run([&](ExecutionState) {
        CHECK(executor.run([&nested](ExecutionState) { nested = true; }));

        // Read after the inner ask returned and before this one ends: nothing
        // may have happened yet, because the queue is behind us.
        nestedRanInside = nested.load();
        outerDone.set_value();
    }));

    outerFinished.wait();
    CHECK_FALSE(nestedRanInside);

    std::promise<void> after;
    auto arrived = after.get_future();
    REQUIRE(executor.run([&after](ExecutionState) { after.set_value(); }));
    arrived.wait();

    // And it did run, in its turn, which is the other half: queued is not
    // dropped.
    CHECK(nested);
}

TEST_CASE("An executor knows whether this is its own thread", "[engine][control]") {
    adapter::SerialControlThread executor;

    CHECK_FALSE(executor.isCurrent());

    std::promise<bool> inside;
    auto answered = inside.get_future();

    REQUIRE(executor.run(
        [&executor, &inside](ExecutionState) { inside.set_value(executor.isCurrent()); }));

    CHECK(answered.get());
}

TEST_CASE("Work that never ran is cancelled rather than dropped", "[engine][control]") {
    // The promise a caller above this makes is exactly one answer, and an
    // executor that let queued work go would leave that promise unkept by
    // whoever made it: a capture waiting for a snapshot nobody is left to send.
    // So accepted work is always called, on this executor, and told which of the
    // two it is.
    //
    // Destroyed from inside its own work, which is the one ordering a test can
    // pin without a sleep in it: the destructor runs while the worker is here,
    // so the item queued below is still queued when it does, and the drain that
    // follows is on this thread rather than a race with it.
    auto executor = std::make_shared<adapter::SerialControlThread>();

    std::promise<ExecutionState> abandoned;
    auto answered = abandoned.get_future();

    REQUIRE(executor->run([executor, &abandoned](ExecutionState) mutable {
        // Queued while the executor is still taking work, and never reached:
        // what follows stops it.
        CHECK(executor->run([&abandoned](ExecutionState state) { abandoned.set_value(state); }));

        // The last reference, dropped here. The destructor runs on this thread,
        // marks the executor stopping, and leaves the worker to answer what is
        // left once this returns.
        executor.reset();
    }));

    executor.reset();

    REQUIRE(answered.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(answered.get() == ExecutionState::Cancelled);
}

TEST_CASE("An executor destroyed by its own work does not wait for itself", "[engine][control]") {
    // A completion is entitled to close the thing that owns the executor -- a
    // project closing from a callback is an ordinary outcome -- and that puts
    // the last reference on the executor's own thread. A thread cannot wait for
    // itself, so this test is one the binary either survives or does not.
    auto executor = std::make_shared<adapter::SerialControlThread>();

    std::promise<void> ran;
    auto finished = ran.get_future();

    REQUIRE(executor->run([executor, &ran](ExecutionState) mutable {
        // The work holds the only other reference. Dropping the outer one here
        // leaves this lambda owning the executor, and the lambda is destroyed
        // on the executor's own thread once this returns.
        ran.set_value();
        executor.reset();
    }));

    executor.reset();
    finished.wait();

    // Nothing to assert but arriving here: a self-join would have taken the
    // process with it.
    SUCCEED();
}
