#include "clip/ClipVoice.hpp"

#include <algorithm>

#include "clip/EventPlacement.hpp"
#include "clip/FadeCurves.hpp"
#include "io/ClipPlacement.hpp"

namespace magda::engine {

void ClipVoice::prepare(const RenderContext& context) {
    sampleRate_ = context.sampleRate;
    release();
}

void ClipVoice::release() {
    clipId_ = INVALID_CLIP_ID;
    eventId_ = INVALID_EVENT_ID;
    sounded_ = false;
}

void ClipVoice::applyFade(juce::dsp::AudioBlock<float> region, int regionFirstSample,
                          const BlockInfo& block, double startSeconds, double endSeconds,
                          FadeCurve curve, bool rising) const {
    const auto length = endSeconds - startSeconds;
    if (!(length > 0.0) || block.numSamples <= 0)
        return;

    const auto blockSeconds = block.endSeconds - block.startSeconds;
    if (!(blockSeconds > 0.0))
        return;

    // The samples of the block the fade covers, narrowed to the ones this
    // region holds. sampleForTime clamps to the block, so a fade that began
    // before it or runs past it contributes the part that is here.
    const auto count = static_cast<int>(region.getNumSamples());
    const auto from = std::max(regionFirstSample, block.sampleForTime(startSeconds));
    const auto to = std::min(regionFirstSample + count, block.sampleForTime(endSeconds));
    if (to <= from)
        return;

    const auto secondsPerSample = blockSeconds / block.numSamples;
    const auto channels = region.getNumChannels();

    for (auto sample = from; sample < to; ++sample) {
        const auto seconds = block.startSeconds + sample * secondsPerSample;
        const auto progress = static_cast<float>((seconds - startSeconds) / length);
        const auto gain = fadeGain(curve, rising ? progress : 1.0f - progress);

        const auto index = static_cast<std::size_t>(sample - regionFirstSample);
        for (std::size_t channel = 0; channel < channels; ++channel)
            region.getChannelPointer(channel)[index] *= gain;
    }
}

bool ClipVoice::render(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                       const BlockInfo& block, PrefetchStream& stream,
                       juce::dsp::AudioBlock<float> scratch, juce::dsp::AudioBlock<float> out) {
    // A voice handed a different entry is a new voice: whatever it played
    // before has nothing to do with where this one begins.
    if (!playing(clip.clipId, event.eventId)) {
        clipId_ = clip.clipId;
        eventId_ = event.eventId;
        sounded_ = false;
    }

    const auto nothing = [this] {
        sounded_ = false;
        return false;
    };

    if (!block.playing || block.numSamples <= 0)
        return nothing();

    // What of this block the voice is heard over: the span the lane leaves
    // audible, narrowed to the event's own stretch of it. The silences inside
    // are not part of this, because they mute material that goes on running.
    const auto windowStart =
        std::max({block.startSeconds, clip.span.startSeconds, event.span.startSeconds});
    const auto windowEnd =
        std::min({block.endSeconds, clip.span.endSeconds, event.span.endSeconds});
    if (windowEnd <= windowStart)
        return nothing();

    const auto first = block.sampleForTime(windowStart);
    const auto count = block.sampleForTime(windowEnd) - first;
    if (count <= 0)
        return nothing();

    auto region = scratch.getSubBlock(0, static_cast<std::size_t>(count));

    // Derived from where the timeline is, never from where the last block left
    // off, so a locate needs nothing said about it: the stream sees a position
    // it was not expecting and seeks (#2016).
    //
    // Through the same function the pool pointed the reader with
    // (EventPlacement.hpp), because a reversed event is read in a mirrored
    // file's coordinates and two derivations of that could disagree. A loop
    // wrap is not a position at all any more: it happens inside the reading,
    // below the stream, so this walks straight through one.
    const auto delivered = stream.read(
        sourceSampleAt(placementFor(event, sampleRate_), windowStart, sampleRate_), region, count);

    // The holes, cleared out of what was read rather than skipped over.
    for (const auto& hole : clip.silenced) {
        const auto holeStart = std::max(hole.startSeconds, windowStart);
        const auto holeEnd = std::min(hole.endSeconds, windowEnd);
        if (holeEnd <= holeStart)
            continue;

        const auto from = std::clamp(block.sampleForTime(holeStart) - first, 0, count);
        const auto to = std::clamp(block.sampleForTime(holeEnd) - first, 0, count);
        if (to > from)
            region.getSubBlock(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from))
                .clear();
    }

    // Which of the source's channels are heard. One that is off contributes
    // nothing and the other is heard on both sides rather than the clip going
    // hard to one of them: where a clip sits in the image is what the pan below
    // decides, and silencing an output here would spend that decision on this.
    //
    // A mono source has already fanned out to every channel by the time it gets
    // here (AudioFileReader::read), so turning one of them off copies the same
    // material over itself and leaves it alone, which is the right answer for a
    // file that never had two sides.
    if (!event.leftChannelActive && !event.rightChannelActive) {
        region.clear();
    } else if (event.leftChannelActive != event.rightChannelActive && region.getNumChannels() > 1) {
        const std::size_t heard = event.leftChannelActive ? 0 : 1;
        for (std::size_t channel = 0; channel < region.getNumChannels(); ++channel)
            if (channel != heard)
                region.getSingleChannelBlock(channel).copyFrom(region.getSingleChannelBlock(heard));
    }

    // The span's edges, not the placement's. What the lane leaves audible is
    // what a listener hears begin and end, and the fades the snapshot resolved
    // are the ones that shape it.
    //
    // The clip's pair only. An event carries its own (AudioEventPlayback), and
    // for the single event a clip has today they are the same fade before and
    // after resolution, so applying both would fade the edge twice. Interior
    // event fades arrive with the clips that have interior events (#1901).
    //
    // Both behaviours play as gain fades here, the speed ramp included, and
    // that is a decision rather than an oversight: a ramp is a stretch, there
    // is no stretcher until #2037, and the alternative to fading it is not
    // fading the edge at all.
    applyFade(region, first, block, clip.span.startSeconds,
              clip.span.startSeconds + clip.fadeInSeconds, clip.fadeInCurve, true);
    applyFade(region, first, block, clip.span.endSeconds - clip.fadeOutSeconds,
              clip.span.endSeconds, clip.fadeOutCurve, false);

    // Volume and gain summed, panned the way the incumbent pans a clip: linear,
    // and hotter on one side rather than quieter on the other. Not a law with a
    // centre correction, because a bounce has to match what was heard.
    //
    // The event's own trim sits under the clip's rather than replacing it, so
    // one event of several can be levelled against the others without touching
    // what the clip plays at. Summed in decibels, which is where they are both
    // expressed and where adding them is what a listener means by it.
    const auto gain = juce::Decibels::decibelsToGain(clip.gainDb + event.gainDb);
    const auto channels = region.getNumChannels();

    if (channels == 2) {
        const auto panned = clip.pan * gain;
        region.getSingleChannelBlock(0).multiplyBy(gain - panned);
        region.getSingleChannelBlock(1).multiplyBy(gain + panned);
    } else {
        region.multiplyBy(gain);
    }

    // Beginning rather than carrying on: the first block of a voice, the first
    // after the timeline jumped, and the first after the reader came back from
    // an underrun. All three start the material wherever it happens to be.
    if (!sounded_ || !block.continuous)
        applyStartDeClick(region, clip.launchFadeSamples);

    out.getSubBlock(static_cast<std::size_t>(first), static_cast<std::size_t>(count)).add(region);

    // Silence the reader could not fill is not sounding, and a block it only
    // half filled ends in that silence as surely as one it missed entirely.
    // Either way the block that resumes starts mid-material, and saying this
    // voice carried on would leave it with nothing to take the step out of it.
    sounded_ = delivered == count;
    return true;
}

}  // namespace magda::engine
