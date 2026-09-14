#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <functional>
#include <memory>
#include <utility>

#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"
#include "core/MacroInfo.hpp"
#include "core/ModInfo.hpp"
#include "core/SelectionManager.hpp"
#include "custom_ui/SamplerUI.hpp"
#include "params/ParamSlotComponent.hpp"
#include "slot/DeviceCustomUIManager.hpp"
#include "slot/DeviceSlotTraits.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"
#include "ui/components/mixer/LevelMeter.hpp"

namespace tracktion::inline engine {
class Plugin;
}

namespace magda::daw::audio {
class MagdaDevice;
class MagdaSamplerPlugin;
}  // namespace magda::daw::audio

namespace magda::daw::ui {

class CompiledDevicePanel;
class FaustCustomView;
class FaustUI;

/**
 * @brief A minimal device slot for a single plugin in a pad's FX chain.
 *
 * Layout:
 *   [PluginName    [UI] [On] [x]]   <- 18px header
 *   [                            ]
 *   [ SamplerUI / Param Grid     ]   <- Content
 *   [                            ]
 */
class PadDeviceSlot : public juce::Component, private juce::Timer {
  public:
    PadDeviceSlot();
    ~PadDeviceSlot() override;

    /** @brief The model's pad device a slot shows, and what renders it. */
    struct Binding {
        magda::DeviceInfo device;
        magda::ChainNodePath devicePath;
        /// The instance rendering the device, on either engine. Null while none does.
        std::function<std::shared_ptr<daw::audio::MagdaDevice>()> renderedDevice;
        /// Tracktion's plugin for the device, where that engine hosts one.
        tracktion::engine::Plugin* plugin = nullptr;
        std::function<tracktion::engine::Plugin::Ptr()> livePlugin;
    };

    /** @brief Show a sampler, a MAGDA faceplate or a hosted plugin's parameters for @p binding. */
    void setDevice(Binding binding);
    void clear();
    int getPreferredWidth() const;
    void setPreferredWidth(int width) {
        preferredWidth_ = width;
    }

    bool isCollapsed() const {
        return collapsed_;
    }
    void setCollapsed(bool collapsed);
    void setSelected(bool selected) {
        selected_ = selected;
        repaint();
    }

    // Callbacks
    std::function<void()> onDeleteClicked;
    std::function<void()> onLayoutChanged;
    std::function<void()> onClicked;

    // Provide callbacks for file operations
    std::function<void(const juce::File&)> onSampleDropped;
    std::function<void()> onLoadSampleRequested;

    // Gain and metering (wired from DeviceSlotComponent via PadChainPanel)
    std::function<std::pair<float, float>()> getMeterLevels;
    std::function<void(float)> onGainDbChanged;
    /// The pad device's power was toggled. True means powered on.
    std::function<void(bool)> onPowerChanged;

    /// Show the device's power without reporting a change.
    void setPowered(bool powered);
    void setGainDb(float db);

    /** Set link mode context so param slots / linkable sliders participate in linking. */
    void setLinkContext(magda::DeviceId deviceId, const magda::ChainNodePath& devicePath,
                        const magda::ChainNodePath& linkOwnerPath, const magda::MacroArray* macros,
                        const magda::ModArray* mods, const magda::MacroArray* trackMacros,
                        const magda::ModArray* trackMods, int selectedModIndex,
                        int selectedMacroIndex);

    /** Get all linkable controls (sampler LinkableTextSliders or external ParamSlotComponents). */
    std::vector<LinkableTextSlider*> getLinkableSliders();

    /** Access a param slot for callback wiring (external plugins only). */
    ParamSlotComponent* getParamSlot(int i) {
        return (i >= 0 && i < PLUGIN_PARAM_SLOTS) ? paramSlots_[static_cast<size_t>(i)].get()
                                                  : nullptr;
    }
    static int getParamSlotCount() {
        return PLUGIN_PARAM_SLOTS;
    }
    int getVisibleParamCount() const {
        return visibleParamCount_;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    static constexpr int HEADER_HEIGHT = 16;
    static constexpr int SLOT_WIDTH = 384;  // 8 cols × 48px PARAM_CELL_WIDTH
    static constexpr int SAMPLER_SLOT_WIDTH = 650;
    static constexpr int COLLAPSED_WIDTH = 48;
    static constexpr int PLUGIN_PARAM_SLOTS = 32;
    static constexpr int METER_WIDTH = 8;
    static constexpr int GAIN_SLIDER_WIDTH = 44;

    Binding binding_;
    magda::DeviceId pluginDeviceId_ = magda::INVALID_DEVICE_ID;
    magda::DeviceInfo device_;
    magda::ChainNodePath devicePath_;
    magda::ChainNodePath linkOwnerPath_;
    int preferredWidth_ = SLOT_WIDTH;
    int visibleParamCount_ = 0;
    DeviceSlotTraits traits_;
    bool collapsed_ = false;
    bool selected_ = false;
    bool usingSharedInlineUi_ = false;

    // Header
    juce::Label nameLabel_;
    juce::TextButton deleteButton_;
    std::unique_ptr<magda::SvgButton> uiButton_;
    std::unique_ptr<magda::SvgButton> onButton_;
    TextSlider gainSlider_;

    // Meter strip (right edge of content area)
    magda::LevelMeter levelMeter_;

    // Content — one of these visible at a time
    std::unique_ptr<SamplerUI> samplerUI_;
    std::unique_ptr<CompiledDevicePanel> compiledPanel_;
    std::unique_ptr<FaustUI> faustUI_;
    std::unique_ptr<FaustCustomView> faustCustomView_;
    std::unique_ptr<DeviceCustomUIManager> customUI_;
    std::array<std::unique_ptr<ParamSlotComponent>, PLUGIN_PARAM_SLOTS> paramSlots_;

    void timerCallback() override;

    /// The rendered sampler, if the device is one and something renders it.
    std::shared_ptr<daw::audio::MagdaSamplerPlugin> renderedSampler() const;

    // Readouts, waveform and playhead come off the rendered sampler. Edits go
    // the other way, to the model at devicePath_, and reach it by projection (#2379).
    void setupForSampler();
    void setupForExternalPlugin(tracktion::engine::Plugin* plugin);
    /// A hosted plugin no Tracktion plugin stands for: its parameters as the engine describes them.
    void setupForHostedParameters();
    bool setupForSharedDeviceUi(const magda::DeviceInfo& device);
    void resetSharedInlineUi();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadDeviceSlot)
};

}  // namespace magda::daw::ui
