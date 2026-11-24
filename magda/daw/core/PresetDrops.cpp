// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PresetDrops.hpp"

#include <memory>
#include <vector>

#include "PadCommands.hpp"
#include "TrackCommands.hpp"
#include "TrackManager.hpp"
#include "UndoManager.hpp"

namespace magda {

namespace {

using Kind = PresetManager::PresetRef::Kind;

PresetDropResult failed(const juce::String& title, const juce::String& error) {
    return {false, 0, title, error};
}

PresetDropResult inserted(int count) {
    return {true, count, {}, {}};
}

PresetDropResult loadChain(const PresetManager::PresetRef& ref, const ChainNodePath& chainPath,
                           int insertIndex) {
    constexpr auto title = "Load Chain Preset Failed";
    auto& presets = PresetManager::getInstance();
    std::vector<ChainElement> elements;
    if (!presets.loadChainPreset(ref.name, elements))
        return failed(title, presets.getLastError());

    auto& tracks = TrackManager::getInstance();
    if (chainPath.steps.empty()) {
        if (!tracks.applyChainPreset(chainPath.trackId, std::move(elements)))
            return failed(title, "Failed to apply preset to track.");
        return inserted(0);
    }

    const auto count = static_cast<int>(elements.size());
    if (count == 0)
        return inserted(0);
    if (!tracks.insertChainElementsByPath(chainPath, std::move(elements), insertIndex))
        return failed(title, "Failed to add the preset's devices to the chain.");
    return inserted(count);
}

PresetDropResult loadDevice(const PresetManager::PresetRef& ref, const ChainNodePath& chainPath,
                            int insertIndex) {
    constexpr auto title = "Load Device Preset Failed";
    auto& presets = PresetManager::getInstance();
    DeviceInfo device;
    if (!presets.loadDevicePreset(ref.pluginFolder, ref.name, device))
        return failed(title, presets.getLastError());

    auto& tracks = TrackManager::getInstance();
    const auto added = chainPath.steps.empty()
                           ? tracks.addDeviceToTrack(chainPath.trackId, device, insertIndex)
                           : tracks.addDeviceToChainByPath(chainPath, device, insertIndex);
    if (added == INVALID_DEVICE_ID)
        return failed(title, "This chain can't take the device.");
    return inserted(1);
}

}  // namespace

bool insertRackPreset(const ChainNodePath& chainPath, RackInfo rack, int insertIndex) {
    if (chainPath.trackId == INVALID_TRACK_ID)
        return false;

    std::vector<ChainElement> elements;
    elements.emplace_back(std::make_unique<RackInfo>(std::move(rack)));

    auto command =
        std::make_unique<PasteChainElementsCommand>(chainPath, std::move(elements), insertIndex);
    // The command is only kept, and so only readable, when executeCommand says it mutated.
    const auto* paste = command.get();
    return UndoManager::getInstance().executeCommand(std::move(command)) && paste->didPaste();
}

std::optional<PresetManager::PresetRef> chainDropPreset(const juce::File& file) {
    auto ref = PresetManager::getInstance().classifyPresetFile(file);
    if (ref.has_value() && ref->kind != Kind::Curve)
        return ref;
    return std::nullopt;
}

PresetDropResult loadPresetIntoChain(const PresetManager::PresetRef& ref,
                                     const ChainNodePath& chainPath, int insertIndex) {
    switch (ref.kind) {
        case Kind::Chain:
            return loadChain(ref, chainPath, insertIndex);
        case Kind::Device:
            return loadDevice(ref, chainPath, insertIndex);
        case Kind::Rack: {
            constexpr auto title = "Load Rack Preset Failed";
            auto& presets = PresetManager::getInstance();
            RackInfo rack;
            if (!presets.loadRackPreset(ref.name, rack))
                return failed(title, presets.getLastError());
            std::vector<ChainElement> elements;
            elements.push_back(makeRackElement(std::move(rack)));
            if (!TrackManager::getInstance().insertChainElementsByPath(
                    chainPath, std::move(elements), insertIndex))
                return failed(title, "Failed to add the rack to the chain.");
            return inserted(1);
        }
        case Kind::Curve:
            break;
    }
    return {};
}

PresetDropResult dropPresetIntoChain(const PresetManager::PresetRef& ref,
                                     const ChainNodePath& chainPath, int insertIndex) {
    // A pad's chain is undone by the grid's pad snapshot, which covers every kind.
    if (chainPath.isPadOwned()) {
        const auto gridPath =
            TrackManager::getInstance().findDevicePath(chainPath.getPadOwnerDeviceId());
        if (!gridPath.isValid())
            return {};
        PresetDropResult result;
        editPads(gridPath, "Load Pad Preset",
                 [&] { result = loadPresetIntoChain(ref, chainPath, insertIndex); });
        return result;
    }

    if (ref.kind != Kind::Rack)
        return loadPresetIntoChain(ref, chainPath, insertIndex);

    auto& presets = PresetManager::getInstance();
    RackInfo rack;
    if (!presets.loadRackPreset(ref.name, rack))
        return failed("Load Rack Preset Failed", presets.getLastError());
    if (!insertRackPreset(chainPath, std::move(rack), insertIndex))
        return failed("Load Rack Preset Failed", "Failed to add the rack to the chain.");
    return inserted(1);
}

}  // namespace magda
