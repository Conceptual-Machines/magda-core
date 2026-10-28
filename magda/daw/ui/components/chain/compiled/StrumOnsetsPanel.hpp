// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "compiled/CompiledPluginPresentation.hpp"

namespace magda::daw::ui {

/**
 * @brief The Strum's faceplate beside its cells: where each strummed note lands in time, for
 *        the shape and cycles the device is set to.
 *
 * A preview of the timing shape, not of a particular chord: a fixed number of ticks.
 */
class StrumOnsetsPanel final : public juce::Component, public CompiledDevicePanel {
  public:
    StrumOnsetsPanel() = default;

    juce::Component& component() override {
        return *this;
    }
    void updateFromDevice(const magda::DeviceInfo& device) override;
    void bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice>) override {}
    void setOnParameterChanged(std::function<void(int, float)>) override {}
    int preferredHeight() const override {
        return 160;
    }

    void paint(juce::Graphics& g) override;

  private:
    std::vector<float> onsets_;
    int shape_ = -1;
    int cycles_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StrumOnsetsPanel)
};

}  // namespace magda::daw::ui
