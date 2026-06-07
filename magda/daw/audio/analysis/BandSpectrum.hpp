#pragma once

#include <array>

#include "AudioTapBuffer.hpp"
#include "MaskingDetector.hpp"

namespace magda::daw::audio {

/**
 * @brief Compute per-band energy (dB) over the masking band layout from a
 *        captured mono signal ring, for the masking detector (#1390).
 *
 * Message thread. Reads the most recent FFT frame from `ring`, windows it, runs
 * a forward FFT, and groups bins into the kNumMaskingBands 1/3-octave bands
 * (band b spans [maskingBandEdgeHz(b), maskingBandEdgeHz(b+1)]). Output is
 * roughly dBFS: a full-scale tone reads near 0 dB in its band, silence reads at
 * the floor. Allocates scratch on the message thread (never the audio thread).
 */
void computeMaskingBandsDb(const AudioTapBuffer& ring, double sampleRate,
                           std::array<float, kNumMaskingBands>& outDb);

}  // namespace magda::daw::audio
