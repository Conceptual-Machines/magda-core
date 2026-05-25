#pragma once

#include "plugins/AnalysisTapPlugin.hpp"

namespace magda::daw::audio {

/**
 * @brief Oscilloscope analysis device. Transparent passthrough that taps the
 *        signal into an AudioTapBuffer; OscilloscopeUI renders the waveform.
 */
class OscilloscopePlugin : public AnalysisTapPlugin {
  public:
    explicit OscilloscopePlugin(const te::PluginCreationInfo& info) : AnalysisTapPlugin(info) {}

    static const char* getPluginName() {
        return "Oscilloscope";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Scope";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopePlugin)
};

}  // namespace magda::daw::audio
