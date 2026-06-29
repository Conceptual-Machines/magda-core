#pragma once

#include <juce_core/juce_core.h>

#include "audio/analysis/TrackMeasurer.hpp"
#include "core/DeviceUiContext.hpp"

namespace magda::daw::ui {

class AudioTapTelemetrySource : public magda::DeviceTelemetrySource {
  public:
    virtual size_t writePosition() const = 0;
    virtual size_t readLatest(float* dest, int numSamples) const = 0;
    virtual double sampleRate() const = 0;
    virtual int traceColourIndex() const = 0;
    virtual void setTraceColourIndex(int index) = 0;
};

class OscilloscopeTelemetrySource : public AudioTapTelemetrySource {
  public:
    static constexpr const char* kKey = "oscilloscope";

    juce::String telemetryKey() const override {
        return kKey;
    }

    virtual float timebaseMs() const = 0;
    virtual void setTimebaseMs(float ms) = 0;
};

class SpectrumTelemetrySource : public AudioTapTelemetrySource {
  public:
    static constexpr const char* kKey = "spectrum";

    juce::String telemetryKey() const override {
        return kKey;
    }

    virtual int fftOrder() const = 0;
    virtual void setFftOrder(int order) = 0;
    virtual float slopeDbPerOct() const = 0;
    virtual void setSlopeDbPerOct(float slope) = 0;
    virtual float smoothing() const = 0;
    virtual void setSmoothing(float smoothing) = 0;
};

class LevelsTelemetrySource : public magda::DeviceTelemetrySource {
  public:
    static constexpr const char* kKey = "levels";

    juce::String telemetryKey() const override {
        return kKey;
    }

    virtual void setActive(bool active) = 0;
    virtual magda::daw::audio::TrackMeasurementSnapshot snapshot() const = 0;
};

class NimbusTelemetrySource : public magda::DeviceTelemetrySource {
  public:
    static constexpr const char* kKey = "nimbus";

    juce::String telemetryKey() const override {
        return kKey;
    }

    virtual size_t inputEnvelopeWritePosition() const = 0;
    virtual size_t readInputEnvelope(float* dest, int numSamples) const = 0;
};

}  // namespace magda::daw::ui
