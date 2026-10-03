#pragma once

#include <magda/sdk/analysis/TransientDetector.hpp>
#include <vector>

#include "io/AudioFileReader.hpp"

/**
 * @file TransientDetector.hpp
 * @brief Feeds an AudioFileReader to the SDK transient detector.
 */

namespace magda::engine {

using TransientDetectionSettings = sdk::TransientDetectionSettings;

/**
 * @brief Transient positions in @p reader, in seconds from its start.
 *
 * Reads the first channel from the beginning twice, in the SDK detector's blocks. Empty for a
 * file with no samples or no rate.
 */
std::vector<double> detectTransients(AudioFileReader& reader,
                                     const TransientDetectionSettings& settings);

}  // namespace magda::engine
