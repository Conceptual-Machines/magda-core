// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PluginDrop.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DrumGridPads.hpp"
#include "core/PresetDrops.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/utils/AudioFileTypes.hpp"

namespace magda::daw::ui {

magda::DeviceInfo deviceInfoFromPluginDrag(const juce::DynamicObject& obj) {
    magda::DeviceInfo device;
    device.name = obj.getProperty("name").toString().toStdString();
    device.manufacturer = obj.getProperty("manufacturer").toString().toStdString();
    auto uniqueId = obj.getProperty("uniqueId").toString();
    device.pluginId = uniqueId.isNotEmpty() ? uniqueId
                                            : obj.getProperty("name").toString() + "_" +
                                                  obj.getProperty("format").toString();
    const auto rawCategory = obj.hasProperty("rawCategory")
                                 ? obj.getProperty("rawCategory").toString()
                                 : obj.getProperty("category").toString();
    const auto rawSubcategory = obj.hasProperty("rawSubcategory")
                                    ? obj.getProperty("rawSubcategory").toString()
                                    : obj.getProperty("subcategory").toString();
    device.isInstrument = rawCategory.isNotEmpty()
                              ? rawCategory == "Instrument"
                              : static_cast<bool>(obj.getProperty("isInstrument"));
    if (rawSubcategory == "MIDI")
        device.deviceType = magda::DeviceType::MIDI;
    else if (device.isInstrument)
        device.deviceType = magda::DeviceType::Instrument;
    device.browserCategoryOverride = obj.getProperty("categoryOverride").toString();
    device.uniqueId = obj.getProperty("uniqueId").toString();
    device.fileOrIdentifier = obj.getProperty("fileOrIdentifier").toString();

    const juce::String format = obj.getProperty("format").toString();
    if (format == "VST3")
        device.format = magda::PluginFormat::VST3;
    else if (format == "AU")
        device.format = magda::PluginFormat::AU;
    else if (format == "LV2")
        device.format = magda::PluginFormat::LV2;
    else if (format == "Internal")
        device.format = magda::PluginFormat::Internal;

    return device;
}

bool isChainDroppableFile(const juce::String& path) {
    return isAudioFile(path) || magda::chainDropPreset(juce::File(path)).has_value();
}

int dropFilesIntoChain(const juce::StringArray& files, const magda::ChainNodePath& chainPath,
                       int insertIndex) {
    auto& tm = magda::TrackManager::getInstance();
    int at = insertIndex;
    for (const auto& path : files) {
        if (isAudioFile(path)) {
            // Rooted on what the pad sends, so it plays at the sample's own pitch.
            if (magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::AddDeviceByPathCommand>(
                        chainPath, magda::padSamplerDevice(path, tm.padRootNoteAt(chainPath)), at)))
                ++at;
            continue;
        }
        const auto ref = magda::chainDropPreset(juce::File(path));
        if (!ref.has_value())
            continue;
        const auto result = magda::dropPresetIntoChain(*ref, chainPath, at);
        at += result.elementsInserted;
        if (result.errorTitle.isNotEmpty())
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                                             .withTitle(result.errorTitle)
                                             .withMessage(result.error)
                                             .withButton("OK", 0),
                                         nullptr);
    }
    return at - insertIndex;
}

}  // namespace magda::daw::ui
