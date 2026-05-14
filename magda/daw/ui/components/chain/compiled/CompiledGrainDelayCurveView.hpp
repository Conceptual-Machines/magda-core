#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaGrainDelayCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Tap and grain-window visual for the compiled grain delay.
 *
 * Mirrors the compiled delay's timeline style: echo taps are plotted on a
 * horizontal time axis with height decaying by Feedback. The grain-specific
 * controls add translucent windows around wet taps: Size controls window
 * width, Spray broadens the uncertainty band, and Pitch tilts each window.
 */
class CompiledGrainDelayCurveView final : public juce::Component,
                                          public CompiledDevicePanel,
                                          private juce::Timer {
  public:
    explicit CompiledGrainDelayCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 140;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaGrainDelayCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)>) override {}
    int preferredHeight() const override {
        return getPreferredHeight();
    }
    void paint(juce::Graphics& g) override;

  private:
    void timerCallback() override;
    void resampleFromPlugin();
    float effectiveDelaySeconds() const;

    magda::daw::audio::compiled::MagdaGrainDelayCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float timeMs_ = 500.0f;
    int divisionIndex_ = 0;
    float divisionValue_ = 1.0f;
    bool sync_ = false;
    float sizeMs_ = 120.0f;
    float pitchSt_ = 0.0f;
    float spray_ = 0.0f;
    float feedback_ = 0.30f;
    float mix_ = 0.40f;
    float bpm_ = 120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledGrainDelayCurveView)
};

}  // namespace magda::daw::ui
