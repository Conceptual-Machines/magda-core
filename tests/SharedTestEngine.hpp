#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <stdexcept>

#include "magda/daw/engine/TracktionEngineWrapper.hpp"

namespace magda::test {

/**
 * Provides a single shared TracktionEngineWrapper for all tests.
 *
 * JUCE global singletons (MIDI device broadcaster, async updaters, timers)
 * cannot survive repeated engine creation/destruction within a single process.
 * Creating one engine and reusing it across tests avoids SIGSEGV crashes
 * caused by corrupted global state.
 */
inline TracktionEngineWrapper*& getSharedEnginePtr() {
    static TracktionEngineWrapper* ptr = nullptr;
    return ptr;
}

inline TracktionEngineWrapper& getSharedEngine() {
    auto*& ptr = getSharedEnginePtr();
    if (!ptr) {
        ptr = new TracktionEngineWrapper();
        if (!ptr->initialize()) {
            delete ptr;
            ptr = nullptr;
            throw std::runtime_error("SharedTestEngine: engine.initialize() failed");
        }
    }
    return *ptr;
}

/**
 * Catch2 listener that shuts down the shared engine before static destruction.
 */
struct SharedEngineCleanup : Catch::EventListenerBase {
    using Catch::EventListenerBase::EventListenerBase;

    void testRunEnded(Catch::TestRunStats const&) override {
        auto*& ptr = getSharedEnginePtr();
        if (ptr) {
            ptr->shutdown();
            delete ptr;
            ptr = nullptr;
        }
    }
};

/**
 * Reset transport to a clean state between tests.
 * Call this at the start of each TEST_CASE that uses the shared engine.
 */
inline void resetTransport(TracktionEngineWrapper& engine) {
    auto* edit = engine.getEdit();
    if (!edit)
        return;
    auto& transport = edit->getTransport();
    if (transport.isPlaying() || transport.isRecording()) {
        transport.stop(false, false);
    }
}

}  // namespace magda::test
