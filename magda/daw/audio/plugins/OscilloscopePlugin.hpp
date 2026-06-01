#pragma once

#include "../../core/Config.hpp"
#include "plugins/AnalysisTapPlugin.hpp"

namespace magda::daw::audio {

/**
 * @brief Oscilloscope analysis device. Transparent passthrough that taps the
 *        signal into an AudioTapBuffer; OscilloscopeUI renders the waveform.
 */
class OscilloscopePlugin : public AnalysisTapPlugin {
  public:
    explicit OscilloscopePlugin(const te::PluginCreationInfo& info)
        : AnalysisTapPlugin(info, 262144) {  // ~5.4 s at 48k
        timebaseMsValue.referTo(state, juce::Identifier("timebaseMs"), getUndoManager(),
                                magda::Config::getInstance().getOscilloscopeDefaults().timebaseMs);
    }

    static const char* getPluginName() {
        return "Oscilloscope";
    }
    static const char* xmlTypeName;

    // Display setting (message thread): visible window length in milliseconds.
    float getTimebaseMs() const {
        return timebaseMsValue.get();
    }
    void setTimebaseMs(float ms) {
        timebaseMsValue = juce::jlimit(1.0f, 5000.0f, ms);
    }

    void restorePluginStateFromValueTree(const juce::ValueTree& v) override {
        AnalysisTapPlugin::restorePluginStateFromValueTree(v);  // trace colour
        tracktion::copyPropertiesToCachedValues(v, timebaseMsValue);
    }

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

  private:
    juce::CachedValue<float> timebaseMsValue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopePlugin)
};

}  // namespace magda::daw::audio
