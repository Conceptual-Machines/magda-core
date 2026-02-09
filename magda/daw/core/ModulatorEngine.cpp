#include "ModulatorEngine.hpp"

#include "TrackManager.hpp"

namespace magda {

void ModulatorEngine::updateAllMods(double deltaTime) {
    auto& tm = TrackManager::getInstance();

    // Consume transport state (one-shot flags are cleared on read)
    auto [bpm, justStarted, justLooped] = tm.consumeTransportState();

    tm.updateAllMods(deltaTime, bpm, justStarted, justLooped);
}

}  // namespace magda
