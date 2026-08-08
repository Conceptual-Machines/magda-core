#include "clip/EventPlacement.hpp"

#include <algorithm>
#include <cmath>

#include "clip/FadeCurves.hpp"
#include "core/TimeStretchModes.hpp"

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

double readingRateOf(const AudioEventPlayback& event) {
    auto rate = event.speedRatio;

    if (event.autoTempo && event.interpBpm > 0.0) {
        // The event's own average, taken off the two faces of its span rather
        // than off a tempo map: how many beats it covers and how long they last
        // are both already resolved here, and their ratio is the tempo it sits
        // under. A ramp inside the span averages out, which is what an average
        // is for.
        const auto beats = event.span.lengthBeats();
        const auto seconds = event.span.lengthSeconds();

        rate = beats > 0.0 && seconds > 0.0 ? (beats * 60.0 / seconds) / event.interpBpm : 1.0;
    }

    return std::clamp(rate > 0.0 ? rate : 1.0, kMinStretchRate, kMaxStretchRate);
}

namespace {

/**
 * @brief One edge of @p clip, warped where its fade is a speed ramp.
 *
 * @p position and the region's ends are all in one domain, and which domain
 * that is depends on the event: seconds for a constant ratio, beats for auto
 * tempo. The warp is the same either way, because a ramp is a statement about
 * how fast the material runs and both faces measure that.
 *
 * Outside a ramp, and for a fade that is an ordinary gain envelope, this is
 * where it was handed.
 */
double ramped(const AudioClipPlayback& clip, double position, double start, double end,
              double fadeIn, double fadeOut) {
    if (clip.fadeInBehaviour == 1 && fadeIn > 0.0 && position < start + fadeIn) {
        const auto alpha = (position - start) / fadeIn;
        return start + fadeIn * fadeRampPosition(clip.fadeInCurve, alpha, true);
    }

    if (clip.fadeOutBehaviour == 1 && fadeOut > 0.0 && position > end - fadeOut) {
        const auto alpha = (position - (end - fadeOut)) / fadeOut;
        return (end - fadeOut) + fadeOut * fadeRampPosition(clip.fadeOutCurve, alpha, false);
    }

    return position;
}

}  // namespace

double readingPositionAt(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                         double seconds, double beat, double deviceSampleRate) {
    const auto placement = placementFor(event, deviceSampleRate);
    const auto anchor = static_cast<double>(placement.sourceOffsetSamples);

    // Seconds of the file's own material, which is what the reading is counted
    // in: it was converted to the device's rate below the stream, so a second of
    // it is deviceSampleRate samples whatever the file was recorded at.
    double elapsed = 0.0;

    if (event.autoTempo && event.interpBpm > 0.0) {
        const auto at = ramped(clip, beat, clip.span.startBeat, clip.span.endBeat, clip.fadeInBeats,
                               clip.fadeOutBeats);

        elapsed = (at - event.span.startBeat) * 60.0 / event.interpBpm;
    } else {
        const auto at = ramped(clip, seconds, clip.span.startSeconds, clip.span.endSeconds,
                               clip.fadeInSeconds, clip.fadeOutSeconds);

        // Clamped the way the rate is, so that a project holding a ratio no
        // stretcher will run at still plays something rather than reading a
        // thousand times faster than the disk can answer.
        const auto rate = std::clamp(event.speedRatio > 0.0 ? event.speedRatio : 1.0,
                                     kMinStretchRate, kMaxStretchRate);

        elapsed = (at - event.span.startSeconds) * rate;
    }

    return anchor + elapsed * deviceSampleRate;
}

StretchSetup stretchSetupFor(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                             const RenderContext& context) {
    StretchSetup setup;
    setup.mode = event.timeStretchMode;

    // What the incumbent reads, and the two are not added: a clip following the
    // pitch track transposes by its own offset, and one that is not plays the
    // pitch change its editor set. Auto pitch's other half, the offset from the
    // project's pitch sequence, needs a pitch track the engine does not have
    // yet (#1891); until it does, such a clip plays its transpose alone.
    setup.semitones = event.autoPitch ? static_cast<float>(event.transpose) : event.pitchChange;

    setup.numChannels = context.numChannels;
    setup.sampleRate = context.sampleRate;
    setup.maxBlockSamples = context.maxBlockSize;
    setup.nominalRate = readingRateOf(event);
    setup.followsTempo = event.autoTempo && event.interpBpm > 0.0;
    setup.speedRamp = (clip.fadeInBehaviour == 1 && clip.fadeInSeconds > 0.0) ||
                      (clip.fadeOutBehaviour == 1 && clip.fadeOutSeconds > 0.0);

    // A pitch nothing else asked for a stretcher for. The model's rule upgrades
    // a mode left at Off for auto tempo, warp, a speed ratio and a pitch change,
    // and not for the transpose an auto pitch clip carries
    // (AudioEvent::getEffectiveTimeStretchMode), so a clip that only transposes
    // arrives here asking for semitones with nothing to apply them with. The
    // engine will not silently drop a value it was handed: it takes the default
    // engine, which is what every other route to this arrives at.
    //
    // Not for analog pitch, which is the one kind of pitch that is not a
    // stretcher at all: the model has already folded it into the speed ratio.
    if (setup.mode == time_stretch_mode::kDisabled && !event.analogPitch &&
        std::abs(setup.semitones) > 0.001f)
        setup.mode = time_stretch_mode::kSignalsmith;

    return setup;
}

}  // namespace magda::engine
