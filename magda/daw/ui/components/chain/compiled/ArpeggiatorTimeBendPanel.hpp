#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/RampCurveDisplay.hpp"

namespace magda::daw::ui {

/**
 * @brief The Arpeggiator's faceplate beside its knob row: the time-bend curve and the settings
 *        that shape it (cycles, quantize, subdivision).
 *
 * Depth and skew are slots, edited here by dragging the curve and in the knob row. Cycles, Q,
 * Sub and the hard-angle toggle are settings, written to the model's state document (#2317).
 */
class ArpeggiatorTimeBendPanel final : public juce::Component,
                                       public CompiledDevicePanel,
                                       private juce::Timer {
  public:
    ArpeggiatorTimeBendPanel();
    ~ArpeggiatorTimeBendPanel() override;

    juce::Component& component() override {
        return *this;
    }
    void updateFromDevice(const magda::DeviceInfo& device) override;
    void updateFromDevice(const magda::DeviceInfo& device,
                          const ParamLinkContext* linkContext) override;
    void bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged_ = std::move(cb);
    }
    void setLinkContextProvider(
        std::function<std::optional<ParamLinkContext>()> provider) override {
        linkContextProvider_ = std::move(provider);
    }
    int preferredHeight() const override {
        return 200;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    using Arp = magda::daw::audio::ArpeggiatorPlugin;

    void timerCallback() override;
    void syncSettingsFromDevice();
    void settingsEdited();
    void writeParameter(int slot, float value);

    std::shared_ptr<Arp> arp_;
    std::function<void(int, float)> onParameterChanged_;
    std::function<std::optional<ParamLinkContext>()> linkContextProvider_;
    magda::ChainNodePath devicePath_;
    bool hardAngle_ = false;

    RampCurveDisplay curve_;
    magda::DraggableValueLabel cycles_{magda::DraggableValueLabel::Format::Integer};
    magda::DraggableValueLabel quantize_{magda::DraggableValueLabel::Format::Percentage};
    magda::DraggableValueLabel subdivision_{magda::DraggableValueLabel::Format::Integer};
    juce::Rectangle<int> header_;
    juce::Rectangle<int> cyclesTile_, quantizeTile_, subdivisionTile_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpeggiatorTimeBendPanel)
};

}  // namespace magda::daw::ui
