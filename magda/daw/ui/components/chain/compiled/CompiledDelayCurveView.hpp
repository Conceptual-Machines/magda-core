#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaDelayCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Tap-timeline visual for the compiled delay.
 *
 * Draws each echo as a vertical bar on a horizontal time axis, height
 * decaying geometrically by Feedback^N. Cross > 0 stripes adjacent taps
 * left/right to convey ping-pong. Window auto-scales to whichever is
 * larger of "16 taps" or "until amplitude drops below ≈ -40 dB", so
 * tight delays (Time = 50ms) and long ones (Time = 2s) both stay
 * legible. Sync-mode draws a faint quarter-note grid behind the taps.
 *
 * Polls host params at ~30 Hz and only repaints when Time / Division /
 * Sync / Feedback / Cross / project tempo crosses an epsilon — the
 * timeline is a static-when-still picture, no animation needed.
 */
class CompiledDelayCurveView final : public juce::Component,
                                     public CompiledDevicePanel,
                                     private juce::Timer {
  public:
    explicit CompiledDelayCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 140;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaDelayCompiledPlugin* plugin);
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

    magda::daw::audio::compiled::MagdaDelayCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    // Cached display values driven by the host params. Repaints fire only
    // when one of these moves materially.
    float timeMs_ = 250.0f;
    int divisionIndex_ = 0;
    float divisionValue_ = 1.0f;  // 1.0 = quarter note (Faust raw value)
    bool sync_ = false;
    float feedback_ = 0.45f;
    float cross_ = 0.0f;
    float bpm_ = 120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledDelayCurveView)
};

}  // namespace magda::daw::ui
