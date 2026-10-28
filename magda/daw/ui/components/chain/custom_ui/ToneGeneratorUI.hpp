// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/Config.hpp"
#include "custom_ui/ToneGeneratorLayout.hpp"
#include "ui/components/common/SegmentedChoice.hpp"

namespace magda::daw::ui {

/// The Test Tone's faceplate: its waveform as a row of glyphs, the noise colour and Transport Only,
/// over the Frequency and Level cells the param grid draws in the Device controls style (#3164).
class ToneGeneratorUI final : public juce::Component,
                              public CompiledDevicePanel,
                              private magda::ConfigListener {
  public:
    ToneGeneratorUI();
    ~ToneGeneratorUI() override;

    juce::Component& component() override {
        return *this;
    }
    void updateFromDevice(const magda::DeviceInfo& device) override;
    void bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice>) override {}
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged_ = std::move(cb);
    }
    int preferredHeight() const override {
        return 200;
    }

    void resized() override;

  private:
    void configChanged() override;
    void writeParameter(int slot, float value);
    void refreshNoiseColor();

    magda::DeviceInfo snapshot_;
    std::function<void(int, float)> onParameterChanged_;
    ParamControlStyle style_ = ParamControlStyle::Text;

    magda::SegmentedChoice waveform_;
    magda::SegmentedChoice noiseColor_;
    magda::SegmentedChoice transportOnly_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneGeneratorUI)
};

}  // namespace magda::daw::ui
