#pragma once

/**
 * @file CoreAudioRate.hpp
 * @brief Changing a CoreAudio interface's sample rate before it streams (#2950).
 */

#include <string>

namespace magda::coreaudio {

/** @brief The interface's nominal rate, or 0 when there is no CoreAudio interface of that name. */
double nominalSampleRate(const std::string& interfaceName);

/**
 * @brief Set @p interfaceName to @p sampleRate and wait until the device and every stream on it
 *        report that rate, or @p timeoutMs passes.
 *
 * An interface still switching while a stream runs reports the rate late and JUCE restarts
 * the stream on that report, so waiting here leaves one start instead of two (#2950).
 * Returns the milliseconds it took, or -1 when the rate did not settle in time.
 */
int settleSampleRate(const std::string& interfaceName, double sampleRate, int timeoutMs);

}  // namespace magda::coreaudio
