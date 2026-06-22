#pragma once

#include "magda/daw/engine/TracktionEngineWrapper.hpp"

namespace magda::test {

inline bool& sharedEngineInitializedFlag() {
    static bool initialized = false;
    return initialized;
}

inline TracktionEngineWrapper& sharedEngineInstance() {
    static TracktionEngineWrapper engine;
    return engine;
}

/**
 * Provides a single shared TracktionEngineWrapper for all tests.
 *
 * JUCE global singletons (MIDI device broadcaster, async updaters, timers)
 * cannot survive repeated engine creation/destruction within a single process.
 * Creating one engine and reusing it across tests avoids SIGSEGV crashes
 * caused by corrupted global state.
 */
inline TracktionEngineWrapper& getSharedEngine() {
    auto& engine = sharedEngineInstance();
    auto& initialized = sharedEngineInitializedFlag();
    if (!initialized) {
        engine.initialize();
        initialized = true;
    }
    return engine;
}

inline TracktionEngineWrapper* getSharedEngineIfInitialized() {
    if (!sharedEngineInitializedFlag())
        return nullptr;
    return &sharedEngineInstance();
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
