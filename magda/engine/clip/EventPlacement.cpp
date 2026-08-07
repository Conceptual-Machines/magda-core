#include "clip/EventPlacement.hpp"

#include <cmath>

namespace magda::engine {

namespace {

/// The rate the event's source-domain counts are in. A source nothing has
/// probed has no rate, and its counts are then read as the device's own
/// samples: converting them by a guess would move material the model has said
/// nothing wrong about.
double sourceRateOf(const AudioEventPlayback& event, double deviceSampleRate) {
    return event.sourceSampleRate > 0.0 ? event.sourceSampleRate : deviceSampleRate;
}

std::int64_t samplesIn(double seconds, double rate) {
    return static_cast<std::int64_t>(std::llround(seconds * rate));
}

/// Source samples the event reads, which is the stretch a mirrored read turns
/// about. One per output sample, at unity speed: what makes this a different
/// number is the stretcher, and it arrives with one (#2037).
std::int64_t regionOf(const AudioEventPlayback& event, double sourceRate) {
    return samplesIn(event.span.lengthSeconds(), sourceRate);
}

std::int64_t floorMod(std::int64_t value, std::int64_t modulus) {
    const auto remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

}  // namespace

SourceRead sourceReadFor(const AudioEventPlayback& event, double deviceSampleRate) {
    const auto sourceRate = sourceRateOf(event, deviceSampleRate);

    SourceRead how;
    how.lengthInSamples = samplesIn(event.sourceDurationSeconds, sourceRate);
    how.sourceSampleRate = sourceRate;
    how.deviceSampleRate = deviceSampleRate;
    how.reversed = event.reversed;

    if (event.loopEnabled) {
        // A loop with no length of its own is the event's own stretch of the
        // file. The model's rule for the field rather than a default chosen
        // here (AudioEvent::loopLengthSamples).
        const auto length =
            event.loopLengthSamples > 0 ? event.loopLengthSamples : regionOf(event, sourceRate);

        if (length > 0) {
            how.loopLengthSamples = length;

            // Mirrored, where the read is. The region's last sample is the
            // first one a backwards read reaches, so the region that was
            // [start, start + length) is [length - start - length, ...) once
            // the file is turned round.
            how.loopStartSamples = event.reversed
                                       ? how.lengthInSamples - (event.loopStartSamples + length)
                                       : event.loopStartSamples;
        }
    }

    return how;
}

ClipPlacement placementFor(const AudioEventPlayback& event, double deviceSampleRate) {
    const auto sourceRate = sourceRateOf(event, deviceSampleRate);
    const auto how = sourceReadFor(event, deviceSampleRate);

    auto anchor = event.anchorSamples;

    if (event.reversed) {
        // What plays first is what played last: the sample at the far end of
        // what this event reads, in the mirrored file's own coordinates. Looped
        // or not is the same question asked of a different stretch, the loop's
        // rather than the event's, because a looped event reads the region
        // round and round and its last sample is wherever the phase had got to.
        //
        // The incumbent works the same value out and writes it back over the
        // clip's offset when the flag is set. Here the model keeps its own
        // coordinates, which is what lets an editor go on showing the region
        // the user chose, and the conversion happens on the way to the reader.
        const auto last = event.anchorSamples + regionOf(event, sourceRate) - 1;

        anchor = how.loopLengthSamples > 0
                     ? how.loopStartSamples + how.loopLengthSamples - 1 -
                           floorMod(last - event.loopStartSamples, how.loopLengthSamples)
                     : how.lengthInSamples - 1 - last;
    }

    // Into the device's samples, which is what the reading is counted in and
    // what the callback consumes one of per output sample.
    const auto scale = sourceRate > 0.0 ? deviceSampleRate / sourceRate : 1.0;

    return ClipPlacement{
        event.span.startSeconds, event.span.endSeconds,
        static_cast<std::int64_t>(std::llround(static_cast<double>(anchor) * scale))};
}

}  // namespace magda::engine
