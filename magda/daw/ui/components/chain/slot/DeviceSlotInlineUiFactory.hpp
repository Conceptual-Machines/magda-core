#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"
#include "slot/DeviceCustomUIManager.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

class CompiledDevicePanel;
class FaustCustomView;
class FaustUI;
class LinkableTextSlider;
struct DeviceSlotModulationContext;

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
    std::function<tracktion::engine::Plugin::Ptr()> getLivePlugin;
    /// Hands the parameter grid a way to read the device's meters. Only
    /// device families that report values back call it; the rest leave the
    /// grid's supplier null.
    std::function<void(std::function<float(int meterIndex)>)> setMeterSource;
};

struct DeviceSlotInlineUiCallbackContext {
    std::function<magda::ChainNodePath()> getNodePath;
    std::function<void()> onLayoutChanged;
    std::function<void()> onParamModulationChanged;
    std::function<void()> onUpdateModsPanel;
    std::function<void()> onUpdateMacroPanel;
    std::function<void()> onShowDeviceModPanel;
    std::function<void()> onShowDeviceMacroPanel;
    std::function<void(int)> onShowAutomationLane;
    std::function<tracktion::engine::Plugin::Ptr()> getLivePlugin;
};

DeviceSlotInlineUiCallbacks makeDeviceSlotInlineUiCallbacks(
    const DeviceSlotInlineUiCallbackContext& context);

DeviceSlotInlineUiKind createDeviceSlotInlineUi(const magda::DeviceInfo& device,
                                                const DeviceSlotTraits& traits,
                                                const magda::ChainNodePath& nodePath,
                                                juce::Component& parent,
                                                DeviceSlotInlineUiStorage storage,
                                                DeviceSlotInlineUiCallbacks callbacks);

/// Binds the Faust header, its custom view and its meters to the device rendering `nodePath`: the
/// slot can be built before it has a path, and a patch load rebuilds the device under the native
/// engine. Returns whether the binding changed.
bool bindDeviceSlotFaustInlineUi(
    const magda::ChainNodePath& nodePath, FaustUI* faustUI,
    std::unique_ptr<FaustCustomView>& faustCustomView, juce::Component& parent,
    const std::function<void(std::function<float(int meterIndex)>)>& setMeterSource = {});

void refreshDeviceSlotInlineUiPluginBindings(const magda::ChainNodePath& nodePath,
                                             CompiledDevicePanel* compiledPanel,
                                             DeviceCustomUIManager& customUI);

void updateDeviceSlotInlineUi(const magda::DeviceInfo& device, CompiledDevicePanel* compiledPanel,
                              DeviceCustomUIManager& customUI);

void refreshDeviceSlotInlineUiParameterValues(const magda::DeviceInfo& device,
                                              CompiledDevicePanel* compiledPanel,
                                              DeviceCustomUIManager& customUI);

void readAndPushDeviceSlotInlineUiModMatrix(magda::DeviceId deviceId,
                                            DeviceCustomUIManager& customUI);

void configureDeviceSlotLinkableSliders(
    const std::vector<LinkableTextSlider*>& sliders, const magda::DeviceInfo& device,
    const magda::ChainNodePath& nodePath, const DeviceSlotModulationContext& context,
    const std::function<void(LinkableTextSlider&)>& configureCallbacks);

}  // namespace magda::daw::ui
