#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaReverbCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Decay-envelope visualisation for the compiled reverb.
 *
 * Two stacked exponential curves on a shared time axis: a low-band tail
 * (uses Decay directly) and a high-band tail (Decay × (1 - Damping)) —
 * so increasing Damping visibly pulls the upper curve away from the lower
 * one. Predelay pushes both curves rightward off the y-axis. Engine name
 * sits top-right.
 */
class CompiledReverbCurveView final : public juce::Component,
                                      public CompiledDevicePanel,
                                      private juce::Timer {
  public:
    explicit CompiledReverbCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 120;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaReverbCompiledPlugin* plugin);
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
    void resampleFromDevice();

    // Approximate t60 (seconds) for the active engine, derived from the
    // engine-aware mapping that lives in each magda_reverb_*.dsp.
    float t60SecondsForEngine(int engineIndex, float decayDisplay) const;

    magda::daw::audio::compiled::MagdaReverbCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    int engine_ = 0;         // 0 = Plate, 1 = Hall, 2 = Room
    float decay_ = 50.0f;    // 0..100, slot value
    float damping_ = 30.0f;  // 0..100, slot value
    float predelayMs_ = 20.0f;

    juce::Rectangle<float> plotArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledReverbCurveView)
};

}  // namespace magda::daw::ui
