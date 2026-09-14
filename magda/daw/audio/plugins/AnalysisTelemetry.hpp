#pragma once

#include <cstddef>
#include <string_view>

#include "plugins/MagdaDevice.hpp"

/**
 * @file AnalysisTelemetry.hpp
 * @brief What an analysis device holds for whatever draws it (#2585).
 *
 * The measurement side of the device SDK: a device fills a ring on the audio
 * thread and something reads it from the message thread, with no engine, no
 * host plugin and no concrete device class between the two. A UI asks its
 * device for one by key (MagdaDevice::telemetry), which is the same question
 * under either engine.
 *
 * The display settings sit beside the ring because they are what scales the
 * drawing of it, and the device is where they are held and persisted.
 */

namespace magda::daw::audio {

/**
 * @brief A mono ring a device fills on the audio thread.
 *
 * Lock-free for one reader: @ref writePosition is the running sample count, so
 * a reader can tell whether anything arrived since it last looked.
 */
class SampleRingTelemetry : public DeviceTelemetry {
  public:
    virtual std::size_t writePosition() const = 0;
    virtual std::size_t readLatest(float* dest, int numSamples) const = 0;
};

/** @brief An analysis tap's ring, with what a trace needs to scale it. */
class AudioTapTelemetry : public SampleRingTelemetry {
  public:
    /// What the tap is fed at, which is what the frequency axis is built from.
    virtual double sampleRate() const = 0;

    virtual int traceColourIndex() const = 0;
    virtual void setTraceColourIndex(int index) = 0;
};

/** @brief The oscilloscope's tap, with the window length it draws. */
class OscilloscopeTelemetry : public AudioTapTelemetry {
  public:
    static constexpr std::string_view kKey = "oscilloscope";

    virtual float timebaseMs() const = 0;
    virtual void setTimebaseMs(float ms) = 0;
};

/** @brief The spectrum analyser's tap, with the transform it draws through. */
class SpectrumTelemetry : public AudioTapTelemetry {
  public:
    static constexpr std::string_view kKey = "spectrum";

    virtual int fftOrder() const = 0;
    virtual void setFftOrder(int order) = 0;
    virtual float slopeDbPerOct() const = 0;
    virtual void setSlopeDbPerOct(float slope) = 0;
    virtual float smoothing() const = 0;
    virtual void setSmoothing(float smoothing) = 0;
};

/** @brief The envelope Nimbus's grain-buffer view draws. */
class GrainEnvelopeTelemetry : public SampleRingTelemetry {
  public:
    static constexpr std::string_view kKey = "nimbus";
};

}  // namespace magda::daw::audio
