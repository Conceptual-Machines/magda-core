#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaModCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Animated LFO visual for the compiled Mod device.
 *
 * Shows two cycles of the active LFO shape with a scrolling playhead. In
 * sync mode draws a beat grid (one vertical line per LFO period). Mode
 * (Trem / Vibrato / Autopan) is labelled in the top-left.
 */
class CompiledModCurveView final : public juce::Component,
                                   public CompiledDevicePanel,
                                   private juce::Timer {
  public:
    explicit CompiledModCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 110;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaModCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    // CompiledDevicePanel
    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged = std::move(cb);
    }
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;

  private:
    void timerCallback() override;
    void resampleFromPlugin();

    float effectiveRateHz() const;
    float lfoSample(double phase) const;

    magda::daw::audio::compiled::MagdaModCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    int mode_ = 0;   // 0 trem, 1 vib, 2 pan
    int shape_ = 0;  // 0 sine, 1 tri, 2 sq, 3 s&h
    bool sync_ = false;
    float rateHz_ = 4.0f;
    float division_ = 1.0f;  // quarter-note multiplier (1.0 = 1/4)
    float depth_ = 0.5f;
    float bpm_ = 120.0f;

    double lfoPhase_ = 0.0;
    juce::uint32 lastTickMs_ = 0;
    juce::Random rng_;  // shared RNG for the S&H trace
    juce::Rectangle<float> plotArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledModCurveView)
};

}  // namespace magda::daw::ui
