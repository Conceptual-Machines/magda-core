// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modulation/ModulatorTriggerSourceMenu.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "core/TrackManager.hpp"

namespace magda::daw::ui {

void showModulatorTriggerSourceMenu(const magda::ChainNodePath& ownerPath, int modIndex) {
    auto& tm = magda::TrackManager::getInstance();
    auto* device = tm.getDeviceInChainByPath(ownerPath);
    auto* rack = device ? nullptr : tm.getRackByPath(ownerPath);
    if (!device && !rack)
        return;

    const auto& sidechain = device ? device->sidechain : rack->sidechain;
    const auto& mods = device ? device->mods : rack->mods;

    // The envelope follower always sources audio; otherwise it follows the trigger mode.
    const bool selValid = modIndex >= 0 && modIndex < static_cast<int>(mods.size());
    const bool isFollower =
        selValid && mods[static_cast<size_t>(modIndex)].type == magda::ModType::Follower;
    const bool isAudioMode =
        isFollower || (selValid && mods[static_cast<size_t>(modIndex)].triggerMode ==
                                       magda::LFOTriggerMode::Audio);
    const auto sidechainType =
        isAudioMode ? magda::SidechainConfig::Type::Audio : magda::SidechainConfig::Type::MIDI;

    juce::PopupMenu menu;

    bool hasSidechain = sidechain.isActive() && sidechain.type == sidechainType;

    menu.addSectionHeader(isAudioMode ? "Audio Trigger Source" : "MIDI Trigger Source");
    menu.addItem(1, "Self", true, !hasSidechain);
    menu.addSeparator();

    struct TrackEntry {
        magda::TrackId id;
        juce::String name;
    };
    auto trackEntries = std::make_shared<std::vector<TrackEntry>>();
    int itemId = 10;
    for (const auto& track : tm.getTracks()) {
        if (track.id == ownerPath.trackId)
            continue;
        bool isCurrent = hasSidechain && sidechain.sourceTrackId == track.id;
        menu.addItem(itemId, track.name, true, isCurrent);
        trackEntries->push_back({track.id, track.name});
        itemId++;
    }

    auto isDeviceTarget = device != nullptr;
    auto deviceId = device ? device->id : magda::INVALID_DEVICE_ID;
    menu.showMenuAsync(juce::PopupMenu::Options(), [isDeviceTarget, deviceId, rackPath = ownerPath,
                                                    trackEntries, sidechainType](int result) {
        if (result == 0)
            return;
        if (result == 1) {
            if (isDeviceTarget)
                magda::TrackManager::getInstance().clearSidechain(deviceId);
            else
                magda::TrackManager::getInstance().clearRackSidechain(rackPath);
        } else {
            int index = result - 10;
            if (index >= 0 && index < static_cast<int>(trackEntries->size())) {
                if (isDeviceTarget) {
                    magda::TrackManager::getInstance().setSidechainSource(
                        deviceId, (*trackEntries)[index].id, sidechainType);
                } else {
                    magda::TrackManager::getInstance().setRackSidechainSource(
                        rackPath, (*trackEntries)[index].id, sidechainType);
                }
            }
        }
    });
}

}  // namespace magda::daw::ui
