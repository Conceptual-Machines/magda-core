#include <atomic>
#include <catch2/catch_test_macros.hpp>
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

TEST_CASE("A serial executor runs its work on one thread, in order", "[engine][control]") {
    adapter::SerialControlThread executor;

    std::vector<int> order;
    std::vector<std::thread::id> threads;
    std::atomic<int> done{0};

    // Queued from several threads at once, which is the case a headless host
    // presents: nothing about who asks says anything about who runs it.
    std::vector<std::thread> askers;
    for (int index = 0; index < 4; ++index)
        askers.emplace_back([&executor, &order, &threads, &done, index] {
            for (int item = 0; item < 8; ++item)
                executor.run([&order, &threads, &done, index] {
                    // No lock: the claim is that this only ever runs on one
                    // thread, so a second one writing here would be the finding
                    // rather than a reason to synchronise.
                    order.push_back(index);
                    threads.push_back(std::this_thread::get_id());
                    ++done;
                });
        });

    for (auto& asker : askers)
        asker.join();

    // Drained by asking for one more thing and waiting for it: work is run in
    // the order it was accepted, so anything queued before this has run by the
    // time this has.
    std::atomic<bool> drained{false};
    executor.run([&drained] { drained = true; });
    while (!drained)
        std::this_thread::yield();

    CHECK(done == 32);
    REQUIRE(order.size() == 32);
    REQUIRE(threads.size() == 32);

    for (const auto& thread : threads)
        CHECK(thread == threads.front());

    // Each asker's own items kept their order, which is what "serial" is worth:
    // interleaving between askers is fine and reordering within one is not.
    for (int index = 0; index < 4; ++index) {
        auto seen = 0;
        for (const auto item : order)
            if (item == index)
                ++seen;

        CHECK(seen == 8);
    }
}

TEST_CASE("Work asked for from the executor runs before the ask returns", "[engine][control]") {
    // What keeps one control operation asking for another from waiting on a
    // thread it is standing on. A capture that loaded a preset, or an editor
    // that saved state on the way down, would otherwise queue behind itself and
    // never be reached.
    adapter::SerialControlThread executor;

    std::atomic<bool> nested{false};
    std::atomic<bool> nestedRanFirst{false};
    std::atomic<bool> finished{false};

    executor.run([&executor, &nested, &nestedRanFirst, &finished] {
        executor.run([&nested] { nested = true; });

        // Read immediately after the inner ask returned: if it had been queued
        // rather than run, nothing would have happened yet.
        nestedRanFirst = nested.load();
        finished = true;
    });

    while (!finished)
        std::this_thread::yield();

    CHECK(nested);
    CHECK(nestedRanFirst);
}

TEST_CASE("An executor knows whether this is its own thread", "[engine][control]") {
    // What lets an implementation tell "run it now" from "queue it", and what a
    // caller checks when it wants to know where its answer arrived.
    adapter::SerialControlThread executor;

    CHECK_FALSE(executor.isCurrent());

    std::atomic<bool> insideSaysYes{false};
    std::atomic<bool> finished{false};

    executor.run([&executor, &insideSaysYes, &finished] {
        insideSaysYes = executor.isCurrent();
        finished = true;
    });

    while (!finished)
        std::this_thread::yield();

    CHECK(insideSaysYes);
}
