#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "core/ControlTarget.hpp"
#include "core/DeviceInfo.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "core/TypeIds.hpp"

namespace tracktion {
inline namespace engine {
class Plugin;
}
}  // namespace tracktion

namespace magda::daw::ui {

class DrumGridUI;
class NodeComponent;

namespace drum_grid_slot {

bool isDrumGridPluginId(const juce::String& pluginId);

void applySlotName(NodeComponent& slot, bool isDrumGrid, const juce::String& deviceName);

bool paintHeaderLogo(juce::Graphics& g, bool isDrumGrid, bool collapsed, int headerHeight,
                     int componentWidth, const juce::Component* modButton,
                     std::initializer_list<const juce::Component*> rightEdgeButtons);

std::optional<juce::Point<float>> getControllerIndicatorAnchor(bool isDrumGrid, bool collapsed,
                                                               int headerHeight,
                                                               const juce::Component* modButton);

bool paintContentHeader(juce::Graphics& g, bool isDrumGrid, bool bypassed,
                        juce::Rectangle<int> textArea);

bool shouldShowModButton(bool isDrumGrid, magda::DeviceType deviceType);

bool shouldShowMacroButton(bool isDrumGrid, magda::DeviceType deviceType, bool isArpeggiator,
                           bool isStepSequencer);

bool shouldShowSidechainButton(bool isDrumGrid, bool canSidechain,
                               bool supportsExternalMidiInputRouting);

bool shouldShowCollapsedUiButton(bool isDrumGrid, bool isInternalDevice);

juce::String getCollapsedName(bool isDrumGrid, const juce::String& drumGridName,
                              const juce::String& fallbackName);

std::vector<tracktion::engine::Plugin*> getCollapsedPlugins(const DrumGridUI* drumGridUI);

void setCollapsedPlugins(DrumGridUI* drumGridUI,
                         const std::vector<tracktion::engine::Plugin*>& plugins);

int getPreferredContentWidth(bool isDrumGrid, const DrumGridUI* drumGridUI);

bool layoutDrumGridUI(DrumGridUI* drumGridUI, juce::Rectangle<int> contentArea);

void setPadChainLinkContext(DrumGridUI* drumGridUI, const magda::ChainNodePath& nodePath,
                            const magda::MacroArray* macros, const magda::ModArray* mods,
                            const magda::MacroArray* trackMacros, const magda::ModArray* trackMods,
                            int selectedModIndex, int selectedMacroIndex);

void appendAvailableDevices(const DrumGridUI* drumGridUI,
                            std::vector<std::pair<magda::DeviceId, juce::String>>& devices);

void appendDeviceParamNames(const DrumGridUI* drumGridUI,
                            std::map<magda::DeviceId, std::vector<juce::String>>& paramsByDevice);

struct PadChainLinkCallbacks {
    std::function<magda::ChainNodePath()> getNodePath;
    std::function<void()> updateParamModulation;
    std::function<void()> updateModsPanel;
    std::function<void()> updateMacroPanel;
    std::function<void(int macroIndex, magda::ControlTarget target)> onMacroTargetChanged;
    std::function<void(int paramIndex)> showAutomationLaneForParam;
};

void wirePadChainLinkCallbacks(DrumGridUI* drumGridUI, PadChainLinkCallbacks callbacks);

}  // namespace drum_grid_slot

}  // namespace magda::daw::ui
