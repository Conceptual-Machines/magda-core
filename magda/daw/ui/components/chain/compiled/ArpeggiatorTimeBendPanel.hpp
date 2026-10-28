// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/TimeBendEditor.hpp"

namespace magda::daw::ui {

/**
 * @brief The Arpeggiator's faceplate beside its knob row: the time bend and quantize.
 *
 * Bend amount and cycles are slots, in the knob row. The curve, its span and on/off, quantize
 * and subdivision are settings, written to the model's state document (#2317).
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
        return 220;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    using Arp = magda::daw::audio::ArpeggiatorPlugin;

    void timerCallback() override;
    void syncSettingsFromDevice();
    void settingsEdited();

    std::shared_ptr<Arp> arp_;
    std::function<void(int, float)> onParameterChanged_;
    std::function<std::optional<ParamLinkContext>()> linkContextProvider_;
    magda::ChainNodePath devicePath_;

    TimeBendEditor bend_;
    magda::DraggableValueLabel quantize_{magda::DraggableValueLabel::Format::Percentage};
    magda::DraggableValueLabel subdivision_{magda::DraggableValueLabel::Format::Integer};
    juce::Rectangle<int> header_;
    juce::Rectangle<int> quantizeTile_, subdivisionTile_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpeggiatorTimeBendPanel)
};

}  // namespace magda::daw::ui
