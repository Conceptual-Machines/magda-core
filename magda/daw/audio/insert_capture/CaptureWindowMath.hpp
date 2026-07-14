#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace magda {

/**
 * @brief Where a render block's overlap with a capture window lands, in samples.
 *
 * Pure math shared by InsertCapturePlugin (audio thread) and its unit tests.
 * All positions are derived from edit-time seconds with llround so consecutive
 * blocks map to contiguous file positions.
 */
struct CaptureBlockMapping {
    juce::int64 fileStartSample = 0;  // overlap start relative to the window start
    int bufferOffset = 0;             // offset into the block, in samples
    int numSamples = 0;               // samples of the block to write (0 = none)
    bool windowFinished = false;      // the block starts at/after the window end
};

/**
 * @brief Map a render block [blockStartSec, blockEndSec) of blockNumSamples onto
 *        the capture window [windowStartSec, windowEndSec).
 */
inline CaptureBlockMapping mapBlockToCaptureWindow(double blockStartSec, double blockEndSec,
                                                   int blockNumSamples, double windowStartSec,
                                                   double windowEndSec, double sampleRate) {
    CaptureBlockMapping m;
    if (blockNumSamples <= 0 || sampleRate <= 0.0 || windowEndSec <= windowStartSec)
        return m;

    if (blockStartSec >= windowEndSec) {
        m.windowFinished = true;
        return m;
    }

    const double overlapStart = std::max(blockStartSec, windowStartSec);
    const double overlapEnd = std::min(blockEndSec, windowEndSec);
    if (overlapEnd <= overlapStart)
        return m;  // block entirely before the window

    // Sample-quantise every boundary against the same origins so consecutive
    // blocks produce contiguous file positions with no drift.
    const auto offset = static_cast<int>(std::llround((overlapStart - blockStartSec) * sampleRate));
    const auto length = static_cast<int>(std::llround((overlapEnd - overlapStart) * sampleRate));

    m.bufferOffset = juce::jlimit(0, blockNumSamples, offset);
    m.numSamples = juce::jlimit(0, blockNumSamples - m.bufferOffset, length);
    m.fileStartSample = std::llround((overlapStart - windowStartSec) * sampleRate);
    return m;
}

}  // namespace magda
