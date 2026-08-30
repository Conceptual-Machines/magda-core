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

        // Nothing in a test binary may reach the speakers.
        //
        // initialize() opens the machine's real audio output, and the engine
        // then plays whatever is in a live Edit whether or not a test asked it
        // to. That stayed inaudible for as long as nothing put a sound source
        // in one, and it stopped being inaudible the moment the corpus loaded a
        // real project: AudioBridge listens to TrackManager, the null-diff leg
        // fills that singleton with the project it is about to render, and one
        // of those projects carries four tone generators. A suite that plays a
        // tone out of somebody's monitors while it runs is a suite that gets
        // turned off.
        //
        // The device rather than the runtime. MAGDA_HEADLESS would do this too
        // and would also take the plugin window manager down with it, which a
        // test asserts is there on a desktop platform -- correctly, because
        // that is real behaviour and not an artefact of being a test. What is
        // wanted is narrower: keep the engine, close the output.
        if (auto* wrapped = engine.getEngine())
            wrapped->getDeviceManager().closeDevices();

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
