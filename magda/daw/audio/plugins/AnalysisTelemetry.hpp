#pragma once

#include <cstddef>
#include <magda/sdk/telemetry/Telemetry.hpp>
#include <string_view>

#include "analysis/TrackMeasurer.hpp"

/**
 * @file AnalysisTelemetry.hpp
 * @brief What an analysis device holds for whatever draws it (#2585).
 *
 * The SDK carries the ring, tap and levels surfaces (magda/sdk/telemetry); this adds the
 * base pack's two analysers, whose display settings sit beside the ring because they scale
 * the drawing of it. A UI asks its device for one by key (sdk::Device::telemetry).
 */

namespace magda::daw::audio {

using sdk::AudioTapTelemetry;
using sdk::DeviceTelemetry;
using sdk::LevelsTelemetry;
using sdk::SampleRingTelemetry;

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
