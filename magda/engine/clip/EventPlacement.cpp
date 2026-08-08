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

/// Whether the event's position is derived from the beat face rather than the
/// seconds one. Auto tempo is the obvious case and warp is the other: the model
/// hands both of them source-beat processing at the event's own interpretation
/// (AudioEvent::sourceInstantToTimelineBeats), so playback has to read the same
/// face the model wrote.
///
/// Off the flag rather than off the map having points in it. A warped event
/// with no markers bends nothing and is still interpreted at its own bpm, so
/// reading this off the map would drop such a clip onto the seconds face and
/// stop it following the tempo the moment its last marker was deleted.
bool usesBeatFace(const AudioEventPlayback& event) {
    return (event.autoTempo || event.warpEnabled) && event.interpBpm > 0.0;
}

/// The constant ratio, clamped the way the rate is, so that a project holding a
/// ratio no stretcher will run at still plays something rather than reading a
/// thousand times faster than the disk can answer.
double constantRate(const AudioEventPlayback& event) {
    return std::clamp(event.speedRatio > 0.0 ? event.speedRatio : 1.0, kMinStretchRate,
                      kMaxStretchRate);
}

/**
 * @brief The event's whole extent, in the domain warp markers measure.
 *
 * Which is seconds of the file's own material before warp bends them: the map's
 * warp side. An unwarped event has no bending, so this is simply the material
 * it reads, and the two uses collapse into one number.
 *
 * Exact rather than clamped-average, because the reversed anchor is derived
 * from it: an event whose far end is off by a stretch factor starts playing
 * somewhere it was never placed.
 */
double warpExtentSecondsOf(const AudioEventPlayback& event) {
    if (usesBeatFace(event))
        return event.span.lengthBeats() * 60.0 / event.interpBpm;

    return event.span.lengthSeconds() * constantRate(event);
}

/// Source seconds the event reads, which is the stretch a mirrored read turns
/// about. Its warped extent through the map, because what the file gives up to
/// fill a warped event is not the extent itself but the map's image of it.
double regionSecondsOf(const AudioEventPlayback& event, double sourceRate) {
    const auto extent = warpExtentSecondsOf(event);

    if (event.warp.empty() || !(sourceRate > 0.0))
        return extent;

    const auto anchorSeconds = static_cast<double>(event.anchorSamples) / sourceRate;
    const auto anchorWarp = event.warp.sourceToWarpSeconds(anchorSeconds);

    return event.warp.sourceSecondsAt(anchorWarp + extent) - anchorSeconds;
}

std::int64_t regionOf(const AudioEventPlayback& event, double sourceRate) {
    return samplesIn(regionSecondsOf(event, sourceRate), sourceRate);
}

std::int64_t floorMod(std::int64_t value, std::int64_t modulus) {
    const auto remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

double floorMod(double value, double modulus) {
    const auto remainder = std::fmod(value, modulus);
    return remainder < 0.0 ? remainder + modulus : remainder;
}

/**
 * @brief A warped event's loop region, measured in warp time.
 *
 * Warp is the one thing that cannot let the reading chain do its own tiling.
 * Everywhere else a loop is folded below the stream, so a position climbs
 * forever and a wrap is not a position change at all (io/SourceReaders.hpp);
 * that works because the reading advances linearly, and under warp it does not.
 * A second pass through a warped loop has to bend the same way the first did,
 * and folding a position that has already been through the map would put the
 * fold in the wrong domain: the map would go on extending at slope 1 past the
 * loop's end and every pass after the first would play straight.
 *
 * So a warped loop folds here, in warp time, and @ref sourceReadFor leaves the
 * tiling below switched off. The reading position then saws back at each wrap
 * instead of climbing, which the stream reads as a seek -- one per pass, which
 * is what a warped loop costs and is bounded.
 *
 * Inactive when the event does not loop, does not warp, or has no region.
 */
struct WarpLoop {
    bool active = false;
    double startWarp = 0.0;
    double lengthWarp = 0.0;
};

WarpLoop warpLoopOf(const AudioEventPlayback& event, double sourceRate) {
    WarpLoop loop;

    if (event.warp.empty() || !event.loopEnabled || !(sourceRate > 0.0))
        return loop;

    // The model's rule for a loop with no length of its own: the event's own
    // stretch of the file (AudioEvent::loopLengthSamples).
    const auto length =
        event.loopLengthSamples > 0 ? event.loopLengthSamples : regionOf(event, sourceRate);
    if (length <= 0)
        return loop;

    const auto startSeconds = static_cast<double>(event.loopStartSamples) / sourceRate;
    const auto endSeconds = static_cast<double>(event.loopStartSamples + length) / sourceRate;

    loop.startWarp = event.warp.sourceToWarpSeconds(startSeconds);
    loop.lengthWarp = event.warp.sourceToWarpSeconds(endSeconds) - loop.startWarp;
    loop.active = loop.lengthWarp > 0.0;

    return loop;
}

/**
 * @brief Where a warped event is in its reading, @p elapsedWarp into itself.
 *
 * In fractional device samples, and the one place warp is resolved: both the
 * anchor the pool cues with and the position a block reads at come from here,
 * with different elapsed values, so the two cannot disagree about a map.
 *
 * Reverse walks the map backwards from the far end of what the event reads.
 * That is what playing the material backwards means with the bending left in
 * place: each stretch of file keeps the rate its markers gave it and is heard
 * in the opposite order. The answer is then mirrored into the reading's own
 * coordinates, exactly as an unwarped reversed event's anchor is.
 */
double warpedReadingSample(const AudioEventPlayback& event, double elapsedWarp, double sourceRate,
                           double deviceSampleRate) {
    const auto anchorSeconds = static_cast<double>(event.anchorSamples) / sourceRate;
    const auto anchorWarp = event.warp.sourceToWarpSeconds(anchorSeconds);

    auto at = event.reversed ? anchorWarp + warpExtentSecondsOf(event) - elapsedWarp
                             : anchorWarp + elapsedWarp;

    if (const auto loop = warpLoopOf(event, sourceRate); loop.active)
        at = loop.startWarp + floorMod(at - loop.startWarp, loop.lengthWarp);

    const auto source = event.warp.sourceSecondsAt(at) * sourceRate;

    // Into the mirrored file's coordinates, which is what the reading delivers
    // for a reversed event. No loop variant: the fold above has already put the
    // position inside the region, so mirroring the file mirrors the region with
    // it.
    const auto inReading =
        event.reversed
            ? static_cast<double>(samplesIn(event.sourceDurationSeconds, sourceRate)) - 1.0 - source
            : source;

    return inReading * (sourceRate > 0.0 ? deviceSampleRate / sourceRate : 1.0);
}

}  // namespace

SourceRead sourceReadFor(const AudioEventPlayback& event, double deviceSampleRate) {
    const auto sourceRate = sourceRateOf(event, deviceSampleRate);

    SourceRead how;
    how.lengthInSamples = samplesIn(event.sourceDurationSeconds, sourceRate);
    how.sourceSampleRate = sourceRate;
    how.deviceSampleRate = deviceSampleRate;
    how.reversed = event.reversed;

    // Not for a warped event: its loop folds in warp time, above the map, and
    // tiling here as well would fold it twice (WarpLoop).
    if (event.loopEnabled && event.warp.empty()) {
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

    // A warped event's start is its map evaluated at no elapsed, which is the
    // same function a block reads through. Derived rather than mirrored here,
    // because the map already answers reverse and looping and a second
    // derivation of either could disagree with it.
    if (!event.warp.empty())
        return ClipPlacement{event.span.startSeconds, event.span.endSeconds,
                             static_cast<std::int64_t>(std::llround(
                                 warpedReadingSample(event, 0.0, sourceRate, deviceSampleRate)))};

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

    if (usesBeatFace(event)) {
        // The event's own average, taken off the two faces of its span rather
        // than off a tempo map: how many beats it covers and how long they last
        // are both already resolved here, and their ratio is the tempo it sits
        // under. A ramp inside the span averages out, which is what an average
        // is for.
        const auto beats = event.span.lengthBeats();
        const auto seconds = event.span.lengthSeconds();

        rate = beats > 0.0 && seconds > 0.0 ? (beats * 60.0 / seconds) / event.interpBpm : 1.0;
    }

    // The steepest the map runs anywhere, not its average. A warped event has
    // no single rate -- that is what warp is -- and what a stretcher is sized
    // and primed against has to cover the fastest stretch of it rather than the
    // one it spends the most time at. The clamp below is what keeps that
    // bounded.
    if (!event.warp.empty())
        rate *= event.warp.maxSourcePerWarp();

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
    // Seconds of the file's own material, which is what the reading is counted
    // in: it was converted to the device's rate below the stream, so a second of
    // it is deviceSampleRate samples whatever the file was recorded at.
    //
    // Under warp this is what the markers measure rather than what is read: the
    // map turns the one into the other, and it is the only thing that can.
    double elapsed = 0.0;

    if (usesBeatFace(event)) {
        const auto at = ramped(clip, beat, clip.span.startBeat, clip.span.endBeat, clip.fadeInBeats,
                               clip.fadeOutBeats);

        elapsed = (at - event.span.startBeat) * 60.0 / event.interpBpm;
    } else {
        const auto at = ramped(clip, seconds, clip.span.startSeconds, clip.span.endSeconds,
                               clip.fadeInSeconds, clip.fadeOutSeconds);

        elapsed = (at - event.span.startSeconds) * constantRate(event);
    }

    if (!event.warp.empty())
        return warpedReadingSample(event, elapsed, sourceRateOf(event, deviceSampleRate),
                                   deviceSampleRate);

    const auto placement = placementFor(event, deviceSampleRate);

    return static_cast<double>(placement.sourceOffsetSamples) + elapsed * deviceSampleRate;
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
    // Warp counts as following as much as auto tempo does. The flag means the
    // rate moves inside the event, so the nominal above is an approximation and
    // a clip averaging unity is still stretching in both directions around it.
    setup.followsTempo = usesBeatFace(event) || event.warpEnabled;
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
