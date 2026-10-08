#include "drum_grid/DeviceSlotDrumGridBridge.hpp"

#include <utility>

#include "NodeComponent.hpp"
#include "core/TrackManager.hpp"
#include "drum_grid/DrumGridUI.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui::drum_grid_slot {

bool isDrumGridPluginId(const juce::String& pluginId) {
    return pluginId.containsIgnoreCase("drumgrid");
}

void applySlotName(NodeComponent& slot, bool isDrumGrid, const juce::String& deviceName) {
    if (isDrumGrid) {
        slot.setNodeName("Drum Grid");
        return;
    }

    slot.setNodeName(deviceName);
    slot.setNodeNameFont(FontManager::getInstance().getUIFontBold(10.0f));
}

bool paintContentHeader(juce::Graphics& g, bool isDrumGrid, bool bypassed,
                        juce::Rectangle<int> textArea) {
    if (!isDrumGrid)
        return false;

    const auto dim = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM);
    g.setColour(bypassed ? dim.withAlpha(0.5f) : dim);
    g.setFont(FontManager::getInstance().getUIFont(11.5f));
    g.drawText("MAGDA / Drum Grid", textArea, juce::Justification::centredLeft);
    return true;
}

bool shouldShowModButton(bool isDrumGrid, magda::DeviceType deviceType) {
    // Analysis devices (oscilloscope / spectrum) expose no mods.
    return (deviceType != magda::DeviceType::MIDI && deviceType != magda::DeviceType::Analysis) ||
           isDrumGrid;
}

bool shouldShowMacroButton(bool isDrumGrid, magda::DeviceType deviceType, bool isArpeggiator,
                           bool isStepSequencer) {
    // Analysis devices expose no macros.
    return (deviceType != magda::DeviceType::MIDI && deviceType != magda::DeviceType::Analysis) ||
           isArpeggiator || isStepSequencer || isDrumGrid;
}

bool shouldShowSidechainButton(bool isDrumGrid, bool canSidechain,
                               bool supportsExternalMidiInputRouting) {
    return !isDrumGrid && (canSidechain || supportsExternalMidiInputRouting);
}

bool shouldShowCollapsedUiButton(bool isDrumGrid, bool isInternalDevice) {
    return !isInternalDevice && !isDrumGrid;
}

juce::String getCollapsedName(bool isDrumGrid, const juce::String& drumGridName,
                              const juce::String& fallbackName) {
    return isDrumGrid ? drumGridName : fallbackName;
}

int getPreferredContentWidth(bool isDrumGrid, const DrumGridUI* drumGridUI) {
    return isDrumGrid && drumGridUI != nullptr ? drumGridUI->getPreferredContentWidth() : 0;
}

bool layoutDrumGridUI(DrumGridUI* drumGridUI, juce::Rectangle<int> contentArea) {
    if (drumGridUI == nullptr)
        return false;

    drumGridUI->setBounds(contentArea);
    drumGridUI->setVisible(true);
    return true;
}

void appendAvailableDevices(const magda::DeviceInfo* grid,
                            std::vector<std::pair<magda::DeviceId, juce::String>>& devices) {
    if (grid == nullptr || !grid->pads)
        return;

    for (const auto& pad : grid->pads->chains)
        for (const auto& layer : pad.layers)
            for (const auto* device : layer.getDevices())
                devices.emplace_back(device->id, pad.name + ": " + device->name);
}

void appendDeviceParamNames(const magda::DeviceInfo* grid,
                            std::map<magda::DeviceId, std::vector<juce::String>>& paramsByDevice) {
    if (grid == nullptr || !grid->pads)
        return;

    for (const auto& pad : grid->pads->chains)
        for (const auto& layer : pad.layers)
            for (const auto* device : layer.getDevices())
                paramsByDevice[device->id] = device->paramNamesByIndex();
}
}  // namespace magda::daw::ui::drum_grid_slot
