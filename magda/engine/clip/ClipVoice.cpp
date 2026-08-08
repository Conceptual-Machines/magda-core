#include "clip/ClipVoice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "clip/EventPlacement.hpp"
#include "clip/FadeCurves.hpp"

namespace magda::engine {

void ClipVoice::prepare(const RenderContext& context) {
    sampleRate_ = context.sampleRate;
    maxBlockSamples_ = context.maxBlockSize;
    release();
}

void ClipVoice::release() {
    clipId_ = INVALID_CLIP_ID;
    eventId_ = INVALID_EVENT_ID;
    sounded_ = false;
    primed_ = nullptr;
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
                       const BlockInfo& block, PrefetchStream& stream, ClipStretcher* stretcher,
                       int preRoll, juce::dsp::AudioBlock<float> scratch,
                       juce::dsp::AudioBlock<float> out) {
    // A voice handed a different entry is a new voice: whatever it played
    // before has nothing to do with where this one begins.
    if (!playing(clip.clipId, event.eventId)) {
        clipId_ = clip.clipId;
        eventId_ = event.eventId;
        sounded_ = false;
        primed_ = nullptr;
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

    // Where in the reading the two ends of this window are. Derived from where
    // the timeline is, never from where the last block left off, so a locate
    // needs nothing said about it: the stream sees a position it was not
    // expecting and seeks (#2016).
    //
    // Through the same function the pool pointed the reader with
    // (EventPlacement.hpp), because a reversed event is read in a mirrored
    // file's coordinates, a stretched one consumes its reading at a rate, and
    // two derivations of either could disagree. A loop wrap is not a position at
    // all any more: it happens inside the reading, below the stream, so this
    // walks straight through one.
    const auto opens =
        readingPositionAt(clip, event, windowStart, block.beatAtTime(windowStart), sampleRate_);
    const auto closes =
        readingPositionAt(clip, event, windowEnd, block.beatAtTime(windowEnd), sampleRate_);

    // How much of the reading one output sample of this block costs. Not the
    // event's nominal rate: under a tempo curve or through a speed ramp the two
    // differ, and what a block actually plays is the distance between its own
    // two ends.
    const auto step = (closes - opens) / count;

    // How far ahead of the position wanted the reading is consumed, which is not
    // zero only for the path that lands between samples (ClipStretcher.hpp).
    const auto ahead = stretcher != nullptr ? stretcher->readAheadSamples() : 0;
    const auto readFrom = static_cast<std::int64_t>(std::llround(opens)) + ahead;
    const auto readTo = static_cast<std::int64_t>(std::llround(closes)) + ahead;

    // Rounded at both ends rather than counted forward, so one block's reading
    // ends exactly where the next one's begins and nothing accumulates. What is
    // left over lands in the ratio the stretcher is handed, which is where a
    // fraction of a sample belongs.
    //
    // A clip with no stretcher consumes one sample per sample by definition, and
    // asking for the difference would let a rounding of a hair shorten a block
    // that is not stretched at all.
    //
    // The ceiling is the contract every buffer downstream was sized against
    // (maxReadingSamples), and it is enforced here because it cannot be enforced
    // in the position map: clamping a position would break the rounded ends that
    // make one block's reading continue the last one's. A rate past the ceiling
    // is reachable through auto tempo alone, where the ratio is the project's
    // tempo over a file's own analysed bpm and nothing bounds their quotient.
    // Such a clip reads short and seeks after it, which is wrong the way a
    // clamped ratio is wrong, rather than wrong the way a buffer overrun is.
    const auto wanted = stretcher == nullptr
                            ? count
                            : static_cast<int>(std::clamp<std::int64_t>(
                                  readTo - readFrom, 0, maxReadingSamples(maxBlockSamples_)));

    // The reading sits behind what the block renders, in the same scratch: they
    // are different lengths whenever the clip is not at its file's own speed.
    auto reading = stretcher != nullptr
                       ? scratch.getSubBlock(static_cast<std::size_t>(maxBlockSamples_),
                                             static_cast<std::size_t>(wanted))
                       : region;

    // Beginning rather than carrying on: the first block of a voice, and the
    // first after the timeline jumped. Whatever the stretcher was holding is for
    // somewhere else, and what replaces it is the material leading up to here,
    // read out of the same stream in the same pass so that the block's own read
    // continues it rather than seeking again.
    //
    // Not after an underrun, deliberately, and this is the difference between a
    // reader catching up and one that never does. Priming reads behind the
    // position the block wants, so a stream that came back short and was primed
    // again for it would be sent backwards every block, and every one of those
    // is a seek that guarantees the next block is short too. A stretcher handed
    // silence and then material has a discontinuity in what it is playing, which
    // is what a loop wrap is as well, and neither needs anything said about it.
    // A stretcher this voice has not primed is either its first or one the pool
    // has just put in place of the last, and both need the same thing.
    if (stretcher != nullptr && (primed_ != stretcher || !block.continuous)) {
        stretcher->reset();
        stretcher->prime(stream, readFrom, preRoll, step);
        primed_ = stretcher;
    }

    const auto delivered = stream.read(readFrom, reading, wanted);

    if (stretcher != nullptr)
        stretcher->process(reading, opens - static_cast<double>(readFrom), step, region);

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
    // A gain fade only. An edge whose behaviour is a speed ramp has already been
    // played by the positions this block read at: the material accelerated into
    // the clip instead of rising into it (EventPlacement.hpp), and putting a
    // gain curve on top of that would fade an edge that was never meant to be
    // quiet.
    if (clip.fadeInBehaviour == 0)
        applyFade(region, first, block, clip.span.startSeconds,
                  clip.span.startSeconds + clip.fadeInSeconds, clip.fadeInCurve, true);

    if (clip.fadeOutBehaviour == 0)
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
    //
    // Against what was asked for rather than against the block: a stretched clip
    // consumes a different amount of reading than it renders, and a full block
    // built out of a short read is exactly the case this has to catch.
    sounded_ = delivered == wanted;
    return true;
}

}  // namespace magda::engine
