#pragma once

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/MidiBridge.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

namespace magda::test {

inline void drainJuceAsyncWork() {
    if (auto* engine = getSharedEngineIfInitialized()) {
        if (auto* teEngine = engine->getEngine())
            teEngine->getBackgroundJobs().getPool().removeAllJobs(false, 10000);
    }

    if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating())
        messageManager->runDispatchLoopUntil(10);
}

inline void resetJuceProjectState() {
    auto* engine = getSharedEngineIfInitialized();
    if (engine)
        resetTransport(*engine);

    SelectionManager::getInstance().clearSelection();
    UndoManager::getInstance().clearHistory();
    AutomationManager::getInstance().clearAll();

    auto& clipManager = ClipManager::getInstance();
    clipManager.clearClipboard();
    clipManager.setNoteClipboard({});
    clipManager.clearAllClips();

    auto& trackManager = TrackManager::getInstance();
    trackManager.clearAllTracks();
    trackManager.setAudioEngine(nullptr);

    if (engine) {
        if (auto* audioBridge = engine->getAudioBridge())
            audioBridge->resetTestState();
        if (auto* midiBridge = engine->getMidiBridge())
            midiBridge->resetTestState();
        if (auto* edit = engine->getEdit()) {
            if (auto* ctx = edit->getCurrentPlaybackContext();
                ctx && ctx->isPlaybackGraphAllocated())
                ctx->reallocate();
        }
    }
}

inline void cleanJuceTestState() {
    // Complete work while the shared engine and its current model are still valid.
    drainJuceAsyncWork();
    resetJuceProjectState();
    drainJuceAsyncWork();
}

class ScopedJuceTestState {
  public:
    ScopedJuceTestState() {
        cleanJuceTestState();
    }

    ~ScopedJuceTestState() {
        cleanJuceTestState();
    }

    ScopedJuceTestState(const ScopedJuceTestState&) = delete;
    ScopedJuceTestState& operator=(const ScopedJuceTestState&) = delete;
};

template <typename Fn> void runWithCleanJuceState(Fn&& fn) {
    ScopedJuceTestState state;
    fn();
}

}  // namespace magda::test
