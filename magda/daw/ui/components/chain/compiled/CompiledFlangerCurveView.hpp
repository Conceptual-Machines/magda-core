#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaFlangerCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Comb-notch response visualisation for the compiled flanger.
 *
 * Plots the magnitude response |H(f)| of the flanger's recirculating
 * delay line on a log-frequency axis. Notches sweep horizontally with
 * the LFO (rate, depth), notch depth scales with feedback. The
 * characteristic flanger shape — series of evenly-spaced notches at
 * f₀, 2f₀, 3f₀… where f₀ = 1/(2·delay) — is the live readout.
 */
class CompiledFlangerCurveView final : public juce::Component,
                                       public CompiledDevicePanel,
                                       private juce::Timer {
  public:
    explicit CompiledFlangerCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 130;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaFlangerCompiledPlugin* plugin);
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
    float effectiveRateHz() const;

    magda::daw::audio::compiled::MagdaFlangerCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    bool sync_ = false;
    float rateHz_ = 0.5f;
    float division_ = 1.0f;
    float depth_ = 0.5f;
    float feedback_ = 0.0f;
    float width_ = 0.5f;
    float mix_ = 0.5f;
    float bpm_ = 120.0f;

    double lfoPhase_ = 0.0;
    juce::uint32 lastTickMs_ = 0;
    juce::Rectangle<float> plotArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledFlangerCurveView)
};

}  // namespace magda::daw::ui
