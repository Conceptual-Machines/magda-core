#include <juce_events/juce_events.h>

#include <vector>

#include "magda/daw/audio/plugins/engine/ControlExecutor.hpp"

/**
 * The message-thread executor, which magda_tests has no thread to run (#2270).
 *
 * Its queue is its own rather than the message loop's, so that a caller already
 * on the message thread can run what is waiting instead of returning to the
 * loop to have it run -- which is what a project save needs, since it writes
 * the file the moment the reads are done (#2581).
 */

namespace {

namespace adapter = magda::daw::audio::engine_adapter;

using adapter::ExecutionState;

class ControlExecutorTest final : public juce::UnitTest {
  public:
    ControlExecutorTest() : juce::UnitTest("Message Thread Control Executor", "magda") {}

    void runTest() override {
        testDrainRunsWhatIsQueued();
        testPostRunsWhatADrainDidNotTake();
        testNestedDrainDoesNothing();
        testDrainKeepsTheOrder();
    }

  private:
    /// Lets the message loop deliver what the executor posted, the way a host
    /// returning to its loop would.
    static void pumpMessageLoop() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    void testDrainRunsWhatIsQueued() {
        beginTest("A drain runs what is queued before it returns");

        adapter::MessageThreadControlExecutor executor;

        auto ran = 0;
        for (auto item = 0; item < 4; ++item)
            expect(executor.run([&ran](ExecutionState state) {
                if (state == ExecutionState::Ran)
                    ++ran;
            }));

        expect(ran == 0, "Nothing runs where it was asked for, including here");

        executor.drain();
        expect(ran == 4, "All four have been answered by the time the drain returns");
    }

    void testPostRunsWhatADrainDidNotTake() {
        beginTest("Work nobody drains still runs on the message loop");

        adapter::MessageThreadControlExecutor executor;

        auto ran = 0;
        expect(executor.run([&ran](ExecutionState state) {
            if (state == ExecutionState::Ran)
                ++ran;
        }));

        pumpMessageLoop();
        expect(ran == 1, "The post the executor made is what runs it");

        // The post for an item a drain already took finds nothing rather than
        // running something twice.
        pumpMessageLoop();
        expect(ran == 1, "And it runs once");
    }

    void testNestedDrainDoesNothing() {
        beginTest("A drain from inside work is not a second transaction");

        adapter::MessageThreadControlExecutor executor;

        std::vector<int> order;

        expect(executor.run([&executor, &order](ExecutionState) {
            order.push_back(1);

            // The outer operation still has its plugin open, so the item queued
            // below must not run inside this one.
            executor.run([&order](ExecutionState) { order.push_back(3); });
            executor.drain();

            order.push_back(2);
        }));

        executor.drain();

        // 3 last: the nested drain did nothing, and the outer one reached the
        // item once the work that queued it had ended.
        expect(order == std::vector<int>{1, 2, 3}, "The nested item ran after the work that "
                                                   "queued it, not inside it");
    }

    void testDrainKeepsTheOrder() {
        beginTest("A drain answers in the order the work was accepted");

        adapter::MessageThreadControlExecutor executor;

        std::vector<int> order;
        for (auto item = 0; item < 6; ++item)
            expect(executor.run([&order, item](ExecutionState) { order.push_back(item); }));

        executor.drain();

        expect(order == std::vector<int>{0, 1, 2, 3, 4, 5},
               "One at a time, in the order it arrived");
    }
};

ControlExecutorTest controlExecutorTest;

}  // namespace
