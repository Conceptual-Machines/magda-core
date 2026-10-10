#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "audio/plugins/compiled/MagdaFilterCompiledPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "compiled/SpectrumOverlay.hpp"
#include "core/DeviceInfo.hpp"
#include "params/ParamLinkResolver.hpp"

namespace magda::daw::ui {

class CompiledFilterCurveView final : public juce::Component,
                                      public CompiledDevicePanel,
                                      private juce::Timer {
  public:
    explicit CompiledFilterCurveView(juce::String pluginId);

    static int getPreferredHeight() {
        return 140;
    }

    void updateFromDevice(const magda::DeviceInfo& device,
                          const ParamLinkContext* linkContext) override;
    void setCompiledPlugin(
        std::shared_ptr<magda::daw::audio::compiled::MagdaFilterCompiledPlugin> plugin);

    /// Drive the curve directly from raw values, for hosts that are not the
    /// MagdaFilterCompiledPlugin (e.g. the poly synth's built-in SVF). Bypasses
    /// the device-snapshot / plugin path entirely.
    /// @param engine     0=SVF 1=Ladder 2=Korg35 3=Oberheim 4=SallenKey
    /// @param modeIndex   this view's order: 0=LP 1=BP 2=HP 3=Notch
    /// @param doubleSlope  true = 24 dB/oct (two cascaded stages); false = 12 dB
    void setRawState(int engine, int modeIndex, float cutoffHz, float resonance01, float drive01,
                     bool doubleSlope = false);

    /// Override the curve/fill accent (defaults to ACCENT_POSITIVE). Lets a host
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
    void bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) override;
    /// Set, the faceplate is interactive: drag sets cutoff across and resonance
    /// up and down, scroll sets drive, double-click restores cutoff and resonance.
    void setOnParameterChanged(std::function<void(int, float)> callback) override {
        onParameterChanged_ = std::move(callback);
        setInterceptsMouseClicks(onParameterChanged_ != nullptr, false);
    }
    void setLinkContextProvider(
        std::function<std::optional<ParamLinkContext>()> provider) override {
        linkContextProvider_ = std::move(provider);
    }
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

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
    float minPlotFrequencyHz_ = 20.0f;
    juce::Colour curveColour_;
    bool hasCurveColour_ = false;
    magda::DeviceInfo deviceSnapshot_;
    std::function<std::optional<ParamLinkContext>()> linkContextProvider_;
    std::shared_ptr<magda::daw::audio::compiled::MagdaFilterCompiledPlugin> compiledPlugin_;
    SpectrumOverlay spectrum_;
    std::function<void(int, float)> onParameterChanged_;
    juce::Rectangle<float> plotArea_;  // Where the last paint put the plot.
    bool dragging_ = false;
    float dragStartResonance_ = 0.0f;
    juce::Rectangle<float> plotBounds() const;
    void setFromHandle(float x, float resonance);

    FilterMode modeForIndex() const;
    float responseDbAt(float frequencyHz) const;
    float qValue() const;
    void updateTargetValues();
    bool hasActiveCurveLinks() const;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledFilterCurveView)
};

}  // namespace magda::daw::ui
