#pragma once

#include <juce_core/juce_core.h>

#include <iostream>
#include <mutex>
#include <vector>

/**
 * @file AssertionWatch.hpp
 * @brief The engine's own assertions, readable as a test result (#2075).
 *
 * A jassert is not fatal. An invalid graph is built anyway, the render happens,
 * and the buffer it produces compares perfectly well: Tracktion saying that two
 * of its nodes do not have distinct identities, and the null-diff corpus then
 * certifying that case as a null, is the worst pairing available. A project the
 * engine itself objects to, signed off as parity.
 *
 * So assertions are collected rather than left as console noise. JUCE routes
 * them through Logger::writeToLog when JUCE_LOG_ASSERTIONS is set, which
 * magda_daw sets for test builds because that is where Tracktion is compiled and
 * an assertion's logging is decided where the macro expands.
 *
 * Two things about lifetime, both of them about the same unsynchronised pointer.
 * juce::Logger holds the current logger in a plain raw pointer with no lock, so
 * every read of it races with every write:
 *
 *  - **It is never taken down.** A destructor restoring the previous logger
 *    would hand a dangling pointer to any thread that had read this one and not
 *    yet called through it.
 *  - **It is installed before anything else exists**, from main(), ahead of the
 *    JUCE initialiser and any engine thread. Installing it from a test would
 *    write that pointer while a thread started by an earlier suite could be
 *    reading it.
 *
 * Neither is fixable with a lock here, because the read happens inside JUCE.
 * Being first and staying for ever is what removes the race rather than
 * narrowing it.
 */

namespace magda::test {

class AssertionWatch final : public juce::Logger {
  public:
    /// The one watch. Install it from main() before any thread can run.
    static AssertionWatch& instance() {
        static auto* watch = new AssertionWatch();
        return *watch;
    }

    /// What fired since the last call, and forgets it.
    std::vector<juce::String> take() {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto taken = std::move(assertions_);
        assertions_.clear();
        return taken;
    }

  private:
    AssertionWatch() {
        juce::Logger::setCurrentLogger(this);
    }

    // Never runs. Private so that nobody can stack-allocate one and reintroduce
    // the teardown this type exists to avoid.
    ~AssertionWatch() override = default;

    void logMessage(const juce::String& message) override {
        // The exact prefix JUCE writes, not a substring match. This is the
        // current logger for the whole process, so a test reporting that a case
        // asserted passes through here too, and a substring match would read
        // that line as another assertion.
        if (message.startsWith("JUCE Assertion failure")) {
            const std::lock_guard<std::mutex> lock(mutex_);
            assertions_.push_back(message);
        }

        // Everything still reaches the console, which is what JUCE's own default
        // logger does with it. A watch that swallowed the log would take away
        // the one thing that says which line asserted.
        std::cout << message << std::endl;
    }

    std::mutex mutex_;
    std::vector<juce::String> assertions_;
};

}  // namespace magda::test
