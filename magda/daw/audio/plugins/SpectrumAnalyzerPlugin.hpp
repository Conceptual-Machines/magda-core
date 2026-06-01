#pragma once

#include "../../core/Config.hpp"
#include "plugins/AnalysisTapPlugin.hpp"

namespace magda::daw::audio {

/**
 * @brief Spectrum Analyzer analysis device. Transparent passthrough that taps
 *        the signal into an AudioTapBuffer; SpectrumAnalyzerUI runs the FFT.
 */
class SpectrumAnalyzerPlugin : public AnalysisTapPlugin {
  public:
    explicit SpectrumAnalyzerPlugin(const te::PluginCreationInfo& info)
        : AnalysisTapPlugin(info, 8192) {  // one FFT frame (max 4096) + headroom
        const auto d = magda::Config::getInstance().getSpectrumDefaults();
        auto* um = getUndoManager();
        fftOrderValue.referTo(state, juce::Identifier("fftOrder"), um, d.fftOrder);  // 11 = 2048
        slopeDbPerOctValue.referTo(state, juce::Identifier("slopeDbPerOct"), um, d.slopeDbPerOct);
        smoothingValue.referTo(state, juce::Identifier("smoothing"), um, d.smoothing);
    }

    static const char* getPluginName() {
        return "Spectrum Analyzer";
    }
    static const char* xmlTypeName;

    // Display settings (message thread). FFT order is 11 (2048) or 12 (4096);
    // slope is the display tilt in dB/octave; smoothing is the response speed (0..1).
    int getFftOrder() const {
        return juce::jlimit(11, 12, fftOrderValue.get());
    }
    void setFftOrder(int order) {
        fftOrderValue = juce::jlimit(11, 12, order);
    }
    float getSlopeDbPerOct() const {
        return slopeDbPerOctValue.get();
    }
    void setSlopeDbPerOct(float slope) {
        slopeDbPerOctValue = slope;
    }
    float getSmoothing() const {
        return juce::jlimit(0.05f, 1.0f, smoothingValue.get());
    }
    void setSmoothing(float s) {
        smoothingValue = juce::jlimit(0.05f, 1.0f, s);
    }

    void restorePluginStateFromValueTree(const juce::ValueTree& v) override {
        AnalysisTapPlugin::restorePluginStateFromValueTree(v);  // trace colour
        tracktion::copyPropertiesToCachedValues(v, fftOrderValue, slopeDbPerOctValue,
                                                smoothingValue);
    }

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Spectrum";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

  private:
    juce::CachedValue<int> fftOrderValue;
    juce::CachedValue<float> slopeDbPerOctValue;
    juce::CachedValue<float> smoothingValue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerPlugin)
};

}  // namespace magda::daw::audio
