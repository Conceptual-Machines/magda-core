#pragma once

#include "magda/daw/engine/MagdaAudioEngine.hpp"

namespace magda::test {
inline bool& sharedEngineInitializedFlag() {
    static bool initialized = false;
    return initialized;
}
inline MagdaAudioEngine& sharedEngineInstance() {
    static MagdaAudioEngine engine(AudioEngineOptions{.headless = true});
    return engine;
}
inline MagdaAudioEngine& getSharedEngine() {
    auto& engine = sharedEngineInstance();
    if (!sharedEngineInitializedFlag()) {
        engine.initialize();
        sharedEngineInitializedFlag() = true;
    }
    return engine;
}
inline MagdaAudioEngine* getSharedEngineIfInitialized() {
    return sharedEngineInitializedFlag() ? &sharedEngineInstance() : nullptr;
}
inline void resetTransport(MagdaAudioEngine& engine) {
    engine.stop();
    engine.setLooping(false);
    engine.locate(0.0);
}
}  // namespace magda::test
