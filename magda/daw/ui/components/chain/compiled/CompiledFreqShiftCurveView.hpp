#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaFreqShiftCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Shift-amount visualisation for the compiled frequency shifter.
 *
 * Linear-frequency strip (0–2 kHz) with a fixed reference marker at
 * 500 Hz (the "input"), an arrow to 500 + shift Hz (the "output"), and
 * a big shift readout. Direction-aware: positive shifts the spectrum
 * up, negative shifts down. Spread shows as a small bracket on the
 * arrow head (L vs R divergence).
 */
class CompiledFreqShiftCurveView final : public juce::Component,
                                         public CompiledDevicePanel,
                                         private juce::Timer {
  public:
    explicit CompiledFreqShiftCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 120;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaFreqShiftCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)>) override {}  // read-only view
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    void paint(juce::Graphics& g) override;

  private:
    void timerCallback() override;
    void resampleFromPlugin();

    magda::daw::audio::compiled::MagdaFreqShiftCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float shiftHz_ = 0.0f;
    float feedback_ = 0.0f;
    float mix_ = 0.5f;
    float spread_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledFreqShiftCurveView)
};

}  // namespace magda::daw::ui
