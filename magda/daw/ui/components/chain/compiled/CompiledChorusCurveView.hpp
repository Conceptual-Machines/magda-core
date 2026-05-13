#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaChorusCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Delay-time trajectory visualisation for the compiled chorus.
 *
 * Plots one line per active voice (1–3). Y is the modulated delay in
 * milliseconds, X is scrolling time. Reads at a glance: voice count =
 * line count, depth = wobble amplitude, rate = wobble speed, width =
 * phase spread between lines.
 */
class CompiledChorusCurveView final : public juce::Component,
                                      public CompiledDevicePanel,
                                      private juce::Timer {
  public:
    explicit CompiledChorusCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 120;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaChorusCompiledPlugin* plugin);
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

    magda::daw::audio::compiled::MagdaChorusCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    int voices_ = 2;  // 1..3, count of active delay lines per channel
    bool sync_ = false;
    float rateHz_ = 0.5f;
    float division_ = 1.0f;
    float depth_ = 0.5f;
    float width_ = 0.5f;
    float bpm_ = 120.0f;

    double lfoPhase_ = 0.0;
    juce::uint32 lastTickMs_ = 0;
    juce::Rectangle<float> plotArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledChorusCurveView)
};

}  // namespace magda::daw::ui
