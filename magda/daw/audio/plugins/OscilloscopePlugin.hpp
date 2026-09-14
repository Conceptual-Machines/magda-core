#pragma once

#include "plugins/AnalysisTapPlugin.hpp"
#include "plugins/AnalysisTelemetry.hpp"
#include "plugins/DeviceServices.hpp"

namespace magda::daw::audio {

/**
 * @brief Oscilloscope analysis device. Transparent passthrough that taps the
 *        signal into an AudioTapBuffer; OscilloscopeUI renders the waveform.
 */
class OscilloscopePlugin : public AnalysisTapPlugin, public OscilloscopeTelemetry {
  public:
    explicit OscilloscopePlugin(const DevicePluginDefaults::Oscilloscope& defaults)
        : AnalysisTapPlugin(262144, defaults.traceColour),
          timebaseMs_(defaults.timebaseMs) {}  // ~5.4 s at 48k

    static const char* getPluginName() {
        return "Oscilloscope";
    }
    static const char* xmlTypeName;

    // Display setting (message thread): visible window length in milliseconds.
    float getTimebaseMs() const {
        return timebaseMs_.load(std::memory_order_relaxed);
    }
    void setTimebaseMs(float ms) override {
        timebaseMs_.store(juce::jlimit(1.0f, 5000.0f, ms), std::memory_order_relaxed);
    }

    void flushState(juce::ValueTree& state) override {
        AnalysisTapPlugin::flushState(state);
        state.setProperty("timebaseMs", getTimebaseMs(), nullptr);
    }

    void restoreState(const juce::ValueTree& state) override {
        AnalysisTapPlugin::restoreState(state);
        if (state.hasProperty("timebaseMs"))
            setTimebaseMs(state["timebaseMs"]);
    }

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Scope",
        };
    }

    std::string_view telemetryKey() const override {
        return kKey;
    }

    DeviceTelemetry* telemetry(std::string_view key) override {
        return key == kKey ? this : nullptr;
    }

    const DeviceTelemetry* telemetry(std::string_view key) const override {
        return key == kKey ? this : nullptr;
    }

    // The tap and its settings, as whatever draws them asks for them (#2585).
    std::size_t writePosition() const override {
        return getTapBuffer().writePosition();
    }

    std::size_t readLatest(float* dest, int numSamples) const override {
        return getTapBuffer().readLatest(dest, numSamples);
    }

    double sampleRate() const override {
        return getSampleRate();
    }

    int traceColourIndex() const override {
        return getTraceColourIndex();
    }

    void setTraceColourIndex(int index) override {
        AnalysisTapPlugin::setTraceColourIndex(index);
    }

    float timebaseMs() const override {
        return getTimebaseMs();
    }

  private:
    std::atomic<float> timebaseMs_{10.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscilloscopePlugin)
};

}  // namespace magda::daw::audio
