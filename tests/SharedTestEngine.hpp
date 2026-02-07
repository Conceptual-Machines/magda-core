#pragma once

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
inline TracktionEngineWrapper& getSharedEngine() {
    static TracktionEngineWrapper engine;
    static bool initialized = false;
    if (!initialized) {
        initialized = engine.initialize();
        if (!initialized) {
            throw std::runtime_error("SharedTestEngine: engine.initialize() failed");
        }
    }
    return engine;
}

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
