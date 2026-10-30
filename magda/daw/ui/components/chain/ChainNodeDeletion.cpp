// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChainNodeDeletion.hpp"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <ranges>

#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

namespace {

bool isInside(const magda::ChainNodePath& inner, const magda::ChainNodePath& outer) {
    return inner.trackId == outer.trackId && outer.topLevelDeviceId == magda::INVALID_DEVICE_ID &&
           inner.steps.size() > outer.steps.size() &&
           std::equal(outer.steps.begin(), outer.steps.end(), inner.steps.begin());
}

bool isRemovable(const magda::ChainNodePath& path) {
    const auto type = path.getType();
    return type == magda::ChainNodeType::Rack || type == magda::ChainNodeType::Device ||
           type == magda::ChainNodeType::TopLevelDevice;
}

}  // namespace

void deleteChainNodes(std::vector<magda::ChainNodePath> paths) {
    std::erase_if(paths, [&paths](const auto& path) {
        return !isRemovable(path) || std::ranges::any_of(paths, [&path](const auto& other) {
            return other.getType() == magda::ChainNodeType::Rack && isInside(path, other);
        });
    });
    if (paths.empty())
        return;

    juce::MessageManager::callAsync([paths]() {
        auto& undo = magda::UndoManager::getInstance();
        const bool many = paths.size() > 1;
        if (many)
            undo.beginCompoundOperation("Delete Devices");
        for (const auto& path : paths) {
            if (path.getType() == magda::ChainNodeType::Rack)
                undo.executeCommand(std::make_unique<magda::RemoveRackByPathCommand>(path));
            else
                undo.executeCommand(std::make_unique<magda::RemoveDeviceByPathCommand>(path));
        }
        if (many)
            undo.endCompoundOperation();
    });
}

std::vector<magda::ChainNodePath> chainNodesToDelete(const magda::ChainNodePath& clicked) {
    auto& selection = magda::SelectionManager::getInstance();
    if (selection.isChainNodeSelected(clicked))
        return selection.getSelectedChainNodes();
    return {clicked};
}

}  // namespace magda::daw::ui
