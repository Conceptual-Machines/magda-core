#pragma once

#include <array>
#include <cstddef>

#include "MaskingDetector.hpp"
#include "TrackMeasurer.hpp"

namespace magda::daw::audio {

/**
 * @brief A track's post-fader measurement point, in whichever engine renders it (#1388, #2760).
 *
 * Dormant until enabled. Message thread, except where the renderer feeds it.
 */
class TrackMeasurementTap {
  public:
    virtual ~TrackMeasurementTap() = default;

    virtual void setMeasurementEnabled(bool shouldMeasure) noexcept = 0;
    virtual TrackMeasurementSnapshot getSnapshot() const noexcept = 0;

    /// Mono capture for masking analysis: heavier, so on only during a masking pass.
    virtual void setSpectrumCaptureEnabled(bool shouldCapture) noexcept = 0;
    virtual void getMaskingBandsDb(std::array<float, kNumMaskingBands>& out) const = 0;

    /// The latest @p numSamples of that capture; the ring's running count, 0 while empty.
    virtual std::size_t readLatestSpectrumSamples(float* dest, int numSamples) const noexcept = 0;
    virtual double getSampleRate() const noexcept = 0;
};

}  // namespace magda::daw::audio
