// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModulatorEngine.hpp"

#include "TrackManager.hpp"

namespace magda {

void ModulatorEngine::updateAllMods(double deltaTime) {
    auto& tm = TrackManager::getInstance();

    // Consume transport state (one-shot flags are cleared on read)
    auto [bpm, playing, justStarted, justLooped, justStopped] = tm.consumeTransportState();

    tm.updateAllMods(deltaTime, bpm, justStarted, justLooped, justStopped, playing);
}

}  // namespace magda
