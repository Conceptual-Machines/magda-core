#pragma once

#include "plugins/AnalysisTapPlugin.hpp"

namespace magda::daw::audio {

/**
 * @brief Spectrum Analyzer analysis device. Transparent passthrough that taps
 *        the signal into an AudioTapBuffer; SpectrumAnalyzerUI runs the FFT.
 */
class SpectrumAnalyzerPlugin : public AnalysisTapPlugin {
  public:
    explicit SpectrumAnalyzerPlugin(const te::PluginCreationInfo& info) : AnalysisTapPlugin(info) {}

    static const char* getPluginName() {
        return "Spectrum Analyzer";
    }
    static const char* xmlTypeName;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerPlugin)
};

}  // namespace magda::daw::audio
