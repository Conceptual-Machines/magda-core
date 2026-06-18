#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"
#include "slot/DeviceCustomUIManager.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

class CompiledDevicePanel;
class FaustCustomView;
class FaustUI;

enum class DeviceSlotInlineUiKind {
    Compiled,
    Faust,
    Custom,
};

struct DeviceSlotInlineUiStorage {
    std::unique_ptr<CompiledDevicePanel>& compiledPanel;
    std::unique_ptr<FaustUI>& faustUI;
    std::unique_ptr<FaustCustomView>& faustCustomView;
    DeviceCustomUIManager& customUI;
};

struct DeviceSlotInlineUiCallbacks {
    std::function<void(int, float)> onParameterChanged;
    std::function<void()> onLayoutChanged;
    std::function<void()> onParamModulationChanged;
    std::function<void()> onUpdateModsPanel;
    std::function<void()> onUpdateMacroPanel;
    std::function<void(int, float)> onCompiledParamLinkRequested;
    std::function<void(int, float)> onCompiledParamLinkAmountChanged;
    std::function<void(int)> onShowAutomationLane;
    std::function<magda::ChainNodePath()> getNodePath;
};

DeviceSlotInlineUiKind createDeviceSlotInlineUi(const magda::DeviceInfo& device,
                                                const DeviceSlotTraits& traits,
                                                const magda::ChainNodePath& nodePath,
                                                juce::Component& parent,
                                                DeviceSlotInlineUiStorage storage,
                                                DeviceSlotInlineUiCallbacks callbacks);

}  // namespace magda::daw::ui
