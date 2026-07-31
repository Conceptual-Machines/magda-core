#pragma once

#include "plugins/AnalysisTapPlugin.hpp"
#include "plugins/DeviceServices.hpp"

namespace magda::daw::audio {

/**
 * @brief Spectrum Analyzer analysis device. Transparent passthrough that taps
 *        the signal into an AudioTapBuffer; SpectrumAnalyzerUI runs the FFT.
 */
class SpectrumAnalyzerPlugin : public AnalysisTapPlugin, public DeviceTelemetry {
  public:
    explicit SpectrumAnalyzerPlugin(const DevicePluginDefaults::Spectrum& defaults)
        : AnalysisTapPlugin(8192),
          fftOrder_(defaults.fftOrder),
          slopeDbPerOct_(defaults.slopeDbPerOct),
          smoothing_(defaults.smoothing) {}  // one FFT frame (max 4096) + headroom

    static const char* getPluginName() {
        return "Spectrum Analyzer";
    }
    static const char* xmlTypeName;
    static constexpr std::string_view telemetryKeyValue = "spectrum";

    // Display settings (message thread). FFT order is 11 (2048) or 12 (4096);
    // slope is the display tilt in dB/octave; smoothing is the response speed (0..1).
    int getFftOrder() const {
        return juce::jlimit(11, 12, fftOrder_.load(std::memory_order_relaxed));
    }
    void setFftOrder(int order) {
        fftOrder_.store(juce::jlimit(11, 12, order), std::memory_order_relaxed);
    }
    float getSlopeDbPerOct() const {
        return slopeDbPerOct_.load(std::memory_order_relaxed);
    }
    void setSlopeDbPerOct(float slope) {
        slopeDbPerOct_.store(slope, std::memory_order_relaxed);
    }
    float getSmoothing() const {
        return juce::jlimit(0.05f, 1.0f, smoothing_.load(std::memory_order_relaxed));
    }
    void setSmoothing(float s) {
        smoothing_.store(juce::jlimit(0.05f, 1.0f, s), std::memory_order_relaxed);
    }

    void flushState(juce::ValueTree& state) override {
        AnalysisTapPlugin::flushState(state);
        state.setProperty("fftOrder", getFftOrder(), nullptr);
        state.setProperty("slopeDbPerOct", getSlopeDbPerOct(), nullptr);
        state.setProperty("smoothing", getSmoothing(), nullptr);
    }

    void restoreState(const juce::ValueTree& state) override {
        AnalysisTapPlugin::restoreState(state);
        if (state.hasProperty("fftOrder"))
            setFftOrder(state["fftOrder"]);
        if (state.hasProperty("slopeDbPerOct"))
            setSlopeDbPerOct(state["slopeDbPerOct"]);
        if (state.hasProperty("smoothing"))
            setSmoothing(state["smoothing"]);
    }

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Spectrum",
        };
    }

    std::string_view telemetryKey() const override {
        return telemetryKeyValue;
    }

    DeviceTelemetry* telemetry(std::string_view key) override {
        return key == telemetryKeyValue ? this : nullptr;
    }

    const DeviceTelemetry* telemetry(std::string_view key) const override {
        return key == telemetryKeyValue ? this : nullptr;
    }

  private:
    std::atomic<int> fftOrder_{11};
    std::atomic<float> slopeDbPerOct_{4.5f};
    std::atomic<float> smoothing_{0.5f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerPlugin)
};

}  // namespace magda::daw::audio
