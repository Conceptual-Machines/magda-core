#pragma once

#include <cmath>
#include <cstdint>

/**
 * @file ClipPlacement.hpp
 * @brief Where a file sits on the timeline, and which of its samples plays when.
 *
 * The one mapping from a moment on the timeline to a sample of a file. It lives
 * on its own rather than inside a source because there is more than one source:
 * playback reads through a prefetch stream and cannot wait, an offline render
 * reads straight through the file and may. If each of them worked out where it
 * was in the file, a bounce could disagree with what was heard by a sample and
 * nothing would say which of the two was right.
 */

namespace magda::engine {

/**
 * @brief A file's position on the timeline, in seconds.
 *
 * In seconds rather than beats. The clip layer positions clips in beats, as the
 * model does, and resolves them to seconds through the tempo map before they
 * reach here; a source that had to do it itself would need a tempo map on the
 * audio thread, and there would then be two of them.
 */
struct ClipPlacement {
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    /// The sample that plays at @ref startSeconds. What trimming the front of a
    /// clip moves.
    ///
    /// Of the reading rather than of the file, and the two are the same thing
    /// for a clip that asks nothing of its source. Where it asks for reverse,
    /// looping or another rate, what is read is a file arranged the way it
    /// asked (io/SourceReaders.hpp) and this counts in that file's samples;
    /// clip/EventPlacement.hpp is what converts one into the other, once.
    std::int64_t sourceOffsetSamples = 0;
};

/**
 * @brief The sample that plays at @p seconds on the timeline.
 *
 * At @p sampleRate, the device's rate, not the file's, and that is not a slip.
 * One sample of the reading is consumed per output sample, so a position
 * derived at any other rate would disagree with what playing actually gets
 * through: every block would land somewhere the reader was not expecting, which
 * is a seek, and a mismatched file would spend every callback re-pointing the
 * reader and hearing nothing.
 *
 * Which is why rate conversion happens below this rather than here. A file
 * recorded at another rate is read through a layer that presents it at the
 * device's (io/SourceReaders.hpp), so the samples this counts are always the
 * device's own and this mapping is always the whole of it.
 */
inline std::int64_t sourceSampleAt(const ClipPlacement& placement, double seconds,
                                   double sampleRate) {
    return placement.sourceOffsetSamples +
           static_cast<std::int64_t>(std::llround((seconds - placement.startSeconds) * sampleRate));
}

}  // namespace magda::engine
