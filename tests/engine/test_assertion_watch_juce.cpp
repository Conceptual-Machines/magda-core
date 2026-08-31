#include <juce_core/juce_core.h>

#include <thread>
#include <vector>

#include "AssertionWatch.hpp"

/**
 * The corpus's safety net, against the ways it could quietly stop working
 * (#2075).
 *
 * AssertionWatch is what keeps a graph Tracktion objects to from being certified
 * as a null. Every corpus case is assertion-free today, which is the point and
 * also the problem: with nothing provoking it, the prefix match, the collection
 * and the process-wide installation could all break and the corpus would stay
 * green while quietly certifying whatever came next. So the net is tested
 * directly, by writing the message JUCE writes.
 */

using magda::test::AssertionWatch;

namespace {

/// What juce::logAssertion emits, which is the only thing the watch is looking
/// for. Written through the same call, so this test breaks if JUCE ever routes
/// assertions somewhere else.
juce::String assertionMessage(const char* file, int line) {
    return juce::String("JUCE Assertion failure in ") + file + ":" + juce::String(line);
}

}  // namespace

class AssertionWatchTests : public juce::UnitTest {
  public:
    AssertionWatchTests() : juce::UnitTest("Assertion Watch", "magda") {}

    void runTest() override {
        auto& watch = AssertionWatch::instance();

        beginTest("The watch is installed for the process");
        {
            // Installed from main() before anything can start a thread, so by
            // the time any test runs it is already the current logger. A test
            // that installed it itself would prove nothing about the ordering
            // the race depends on.
            expect(juce::Logger::getCurrentLogger() == &watch,
                   "the watch should already be the current logger");
        }

        beginTest("An assertion is captured");
        {
            watch.take();
            juce::Logger::writeToLog(assertionMessage("Pretend.cpp", 42));

            const auto fired = watch.take();
            expect(fired.size() == 1, "one assertion, got " + juce::String((int)fired.size()));
            if (fired.size() == 1)
                expect(fired.front().contains("Pretend.cpp:42"), fired.front());
        }

        beginTest("Taking twice does not report it twice");
        {
            watch.take();
            juce::Logger::writeToLog(assertionMessage("Once.cpp", 1));

            expect(watch.take().size() == 1);
            expect(watch.take().empty(), "the second take should be empty");
        }

        beginTest("A line that merely mentions one is not an assertion");
        {
            // The runner reports a case that asserted, and that report goes
            // through this logger too. Matching a substring would read it as a
            // second assertion and hand it to the next case.
            watch.take();
            juce::Logger::writeToLog("  mix.volume: " + assertionMessage("Quoted.cpp", 7));
            juce::Logger::writeToLog("nothing to see here");

            expect(watch.take().empty(), "only a message that starts with the prefix counts");
        }

        beginTest("Assertions from several threads all arrive");
        {
            // An assertion fires on whichever thread reached it, including the
            // pool a proxy renders on, while the test thread is taking the list.
            watch.take();

            constexpr int threads = 8;
            constexpr int each = 50;

            std::vector<std::thread> writers;
            writers.reserve(threads);
            for (auto t = 0; t < threads; ++t)
                writers.emplace_back([t] {
                    for (auto i = 0; i < each; ++i)
                        juce::Logger::writeToLog(assertionMessage("Thread.cpp", t * each + i));
                });

            for (auto& writer : writers)
                writer.join();

            const auto fired = watch.take();
            expect(fired.size() == threads * each, "expected " + juce::String(threads * each) +
                                                       " assertions, got " +
                                                       juce::String((int)fired.size()));
        }
    }
};

static AssertionWatchTests assertionWatchTests;
