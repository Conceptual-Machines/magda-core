#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"
#include "params/ParamLinkResolver.hpp"

namespace magda::daw::audio::compiled {
class MagdaFilterCompiledPlugin;
}

namespace magda::daw::ui {

class CompiledFilterCurveView final : public juce::Component,
                                      public CompiledDevicePanel,
                                      private juce::Timer {
  public:
    explicit CompiledFilterCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 140;
    }

    void updateFromDevice(const magda::DeviceInfo& device,
                          const ParamLinkContext* linkContext) override;
    void setCompiledPlugin(magda::daw::audio::compiled::MagdaFilterCompiledPlugin* plugin);

    /// Drive the curve directly from raw values, for hosts that are not the
    /// MagdaFilterCompiledPlugin (e.g. the poly synth's built-in SVF). Bypasses
    /// the device-snapshot / plugin path entirely.
    /// @param engine     0=SVF 1=Ladder 2=Korg35 3=Oberheim 4=SallenKey
    /// @param modeIndex   this view's order: 0=LP 1=BP 2=HP 3=Notch
    /// @param doubleSlope  true = 24 dB/oct (two cascaded stages); false = 12 dB
    void setRawState(int engine, int modeIndex, float cutoffHz, float resonance01, float drive01,
                     bool doubleSlope = false);

    /// Override the curve/fill accent (defaults to ACCENT_GREEN). Lets a host
    /// match the curve to its own theme (e.g. the poly synth's blue envelopes).
    void setCurveColour(juce::Colour colour) {
        curveColour_ = colour;
        hasCurveColour_ = true;
        repaint();
    }

    // CompiledDevicePanel
    juce::Component& component() override {
        return *this;
    }
    void updateFromDevice(const magda::DeviceInfo& device) override {
        updateFromDevice(device, nullptr);
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)>) override {}  // read-only view
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    void paint(juce::Graphics& g) override;

  private:
    enum class FilterFamily { SVF, Ladder, Korg35, Oberheim, SallenKey };
    enum class FilterMode { LowPass, BandPass, HighPass, Notch };

    FilterFamily family_ = FilterFamily::SVF;
    float cutoffHz_ = 1000.0f;
    float resonance_ = 0.0f;
    float drive_ = 0.0f;
    int modeIndex_ = 0;
    bool doublePole_ = false;  // 24 dB/oct: square the magnitude
    float targetCutoffHz_ = 1000.0f;
    float targetResonance_ = 0.0f;
    float targetDrive_ = 0.0f;
    int targetModeIndex_ = 0;
    bool initialised_ = false;
    juce::Colour curveColour_;
    bool hasCurveColour_ = false;
    magda::DeviceInfo deviceSnapshot_;
    ParamLinkContext linkContext_;
    bool hasLinkContext_ = false;
    magda::daw::audio::compiled::MagdaFilterCompiledPlugin* compiledPlugin_ = nullptr;

    FilterMode modeForIndex() const;
    float responseDbAt(float frequencyHz) const;
    float qValue() const;
    void updateTargetValues();
    bool hasActiveCurveLinks() const;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledFilterCurveView)
};

}  // namespace magda::daw::ui
