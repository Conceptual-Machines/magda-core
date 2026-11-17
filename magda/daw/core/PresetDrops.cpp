// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PresetDrops.hpp"

#include <memory>
#include <vector>

#include "ChainNodePath.hpp"
#include "TrackCommands.hpp"
#include "UndoManager.hpp"

namespace magda {

bool insertRackPreset(TrackId trackId, RackInfo rack, int insertIndex) {
    if (trackId == INVALID_TRACK_ID)
        return false;

    std::vector<ChainElement> elements;
    elements.emplace_back(std::make_unique<RackInfo>(std::move(rack)));

    auto command = std::make_unique<PasteChainElementsCommand>(ChainNodePath::trackLevel(trackId),
                                                               std::move(elements), insertIndex);
    // The command is only kept, and so only readable, when executeCommand says it mutated.
    const auto* paste = command.get();
    return UndoManager::getInstance().executeCommand(std::move(command)) && paste->didPaste();
}

}  // namespace magda
