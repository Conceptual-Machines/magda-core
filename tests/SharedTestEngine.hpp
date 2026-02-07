#pragma once
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

namespace magda::test {

inline TracktionEngineWrapper& getSharedEngine() {
    static TracktionEngineWrapper wrapper;
    static bool initialized = false;
    if (!initialized) {
        wrapper.initialize();
        initialized = true;
    }
    return wrapper;
}

/// Call before JUCE teardown (i.e. before ScopedJuceInitialiser_GUI destructs).
inline void shutdownSharedEngine() {
    static bool shutdown = false;
    if (!shutdown) {
        getSharedEngine().shutdown();
        shutdown = true;
    }
}

}  // namespace magda::test
