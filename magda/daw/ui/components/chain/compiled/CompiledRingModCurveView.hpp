#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaRingModCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Carrier visualisation for the compiled ring modulator.
 *
 * Main view: log-frequency axis (10 Hz .. 20 kHz) with a vertical marker
 * at the current carrier frequency. Audible-band guides at the decade
 * lines give an instant sense of "where in the spectrum is the carrier
 * sitting" — low for tremolo, high for the clang regime.
 *
 * Inset (bottom-right): two cycles of the active carrier shape. At low
 * carrier rates it reads as a slow sweep; at audio rate it's
 * effectively a static waveform glyph showing the chosen shape.
 */
class CompiledRingModCurveView final : public juce::Component,
                                       public CompiledDevicePanel,
                                       private juce::Timer {
  public:
    explicit CompiledRingModCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 120;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaRingModCompiledPlugin* plugin);
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
    float effectiveFreqHz() const;

    magda::daw::audio::compiled::MagdaRingModCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    bool sync_ = false;
    float freqHz_ = 100.0f;
    float division_ = 1.0f;
    int shape_ = 0;
    float mix_ = 0.5f;
    float width_ = 0.5f;
    int source_ = 0;  // 0 = oscillator, 1 = sidechain
    float bpm_ = 120.0f;

    double phase_ = 0.0;
    juce::uint32 lastTickMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledRingModCurveView)
};

}  // namespace magda::daw::ui
