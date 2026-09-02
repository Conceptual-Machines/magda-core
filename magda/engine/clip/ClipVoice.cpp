#include "clip/ClipVoice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "clip/EventPlacement.hpp"
#include "clip/FadeCurves.hpp"

namespace magda::engine {

void ClipVoice::prepare(const RenderContext& context) {
    sampleRate_ = context.sampleRate;
    maxBlockSamples_ = stretchWorkSamples(context.maxBlockSize);

    // One cell of output, off the callback. What a cell produces beyond the
    // block that asked for it waits here.
    held_.setSize(std::max(1, context.numChannels), kCellSamples, false, true, false);

    release();
}

void ClipVoice::release() {
    clipId_ = INVALID_CLIP_ID;
    eventId_ = INVALID_EVENT_ID;
    sounded_ = false;
    primed_ = nullptr;
    deClick_.reset();
    pending_ = false;
    pendingCount_ = 0;
    pendingRead_ = 0;
    skip_ = 0;
}

bool ClipVoice::renderThroughCells(const AudioClipPlayback& clip, const AudioEventPlayback& event,
                                   const BlockInfo& block, PrefetchStream& stream,
                                   ClipStretcher& stretcher, int preRoll,
                                   juce::dsp::AudioBlock<float> scratch,
                                   juce::dsp::AudioBlock<float> region, double windowStart,
                                   int count) {
    // The grid, in samples of the timeline, anchored to where the event begins
    // rather than to where this block does. That anchor is the whole point: two
    // renders that cut the timeline into different blocks still divide it into
    // the same cells, so the stretcher is handed the same input in the same
    // order both times and gives back the same samples.
    const auto eventStartSample =
        static_cast<std::int64_t>(std::llround(event.span.startSeconds * sampleRate_));
    const auto windowStartSample =
        static_cast<std::int64_t>(std::llround(windowStart * sampleRate_));

    // Beginning rather than carrying on: the first block of a voice, the first
    // after the timeline jumped, and one whose stretcher the pool has replaced.
    // Not after an underrun, deliberately, and that is the difference between a
    // reader catching up and one that never does: priming reads behind the
    // position wanted, so a stream that came back short and was primed again
    // would be sent backwards every block.
    auto needsPrime = primed_ != &stretcher || !block.continuous || !pending_;

    if (needsPrime) {
        // Back to the cell boundary at or before where playback resumes, and
        // the head of that cell is produced and dropped. Resuming mid-cell
        // would anchor the grid to wherever the transport happened to land,
        // which is the block dependency again wearing a locate's clothes.
        const auto offset = windowStartSample - eventStartSample;
        const auto index = static_cast<std::int64_t>(
            std::floor(static_cast<double>(offset) / static_cast<double>(kCellSamples)));

        nextCell_ = eventStartSample + index * kCellSamples;
        pendingCount_ = 0;
        pendingRead_ = 0;
        skip_ = static_cast<int>(windowStartSample - nextCell_);

        stretcher.reset();
        primed_ = &stretcher;
        pending_ = true;
    }

    auto produced = 0;
    auto full = true;

    while (produced < count) {
        if (pendingRead_ < pendingCount_) {
            const auto take = std::min(pendingCount_ - pendingRead_, count - produced);
            for (std::size_t channel = 0; channel < region.getNumChannels(); ++channel) {
                const auto* source = held_.getReadPointer(static_cast<int>(
                    std::min(channel, static_cast<std::size_t>(held_.getNumChannels() - 1))));
                auto* destination = region.getChannelPointer(channel);
                std::copy(source + pendingRead_, source + pendingRead_ + take,
                          destination + produced);
            }

            pendingRead_ += take;
            produced += take;
            continue;
        }

        // One whole cell, at its own two ends. The ratio a cell runs at is what
        // its own boundaries say, exactly as a block's used to be, and the
        // boundaries no longer move.
        const auto cellStartSeconds = static_cast<double>(nextCell_) / sampleRate_;
        const auto cellEndSeconds = static_cast<double>(nextCell_ + kCellSamples) / sampleRate_;

        const auto opens = readingPositionAt(clip, event, cellStartSeconds,
                                             block.beatAtTime(cellStartSeconds), sampleRate_);
        const auto closes = readingPositionAt(clip, event, cellEndSeconds,
                                              block.beatAtTime(cellEndSeconds), sampleRate_);
        const auto step = (closes - opens) / kCellSamples;

        const auto ahead = stretcher.readAheadSamples();
        const auto readFrom = static_cast<std::int64_t>(std::llround(opens)) + ahead;
        const auto readTo = static_cast<std::int64_t>(std::llround(closes)) + ahead;
        const auto wanted = static_cast<int>(
            std::clamp<std::int64_t>(readTo - readFrom, 0, maxReadingSamples(maxBlockSamples_)));

        if (needsPrime) {
            // At the cell's own boundary rather than at the block's, so what
            // the stretcher is primed with is the material leading up to a
            // fixed instant. Read out of the same stream in the same pass, so
            // the cell's own read continues it rather than seeking again.
            stretcher.prime(stream, readFrom, preRoll, step);
            needsPrime = false;
        }

        auto reading = scratch.getSubBlock(static_cast<std::size_t>(maxBlockSamples_),
                                           static_cast<std::size_t>(wanted));
        const auto delivered = stream.read(readFrom, reading, wanted);

        juce::dsp::AudioBlock<float> cell(held_);
        auto cellOut = cell.getSubBlock(0, static_cast<std::size_t>(kCellSamples));
        stretcher.process(reading, opens - static_cast<double>(readFrom), step, cellOut);

        nextCell_ += kCellSamples;
        pendingCount_ = kCellSamples;
        pendingRead_ = std::min(skip_, kCellSamples);
        skip_ = std::max(0, skip_ - kCellSamples);

        // A cell the reader could not fill is a cell that ends in silence, and
        // the voice is no longer carrying on from anywhere. Said rather than
        // acted on: the block is still filled to its end, exactly as the plain
        // path fills it, because stopping here would leave the rest of the
        // region holding whatever the scratch held last and would put the voice
        // a block behind for the rest of the take.
        if (delivered < wanted)
            full = false;
    }

    return full;
}

void ClipVoice::applyFade(juce::dsp::AudioBlock<float> region, int regionFirstSample,
                          const BlockInfo& block, double startSeconds, double endSeconds,
                          FadeCurve curve, bool rising) const {
    const auto length = endSeconds - startSeconds;
    if (!(length > 0.0) || block.numSamples <= 0)
        return;

    const auto blockSeconds = block.seconds.end - block.seconds.start;
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
        const auto seconds = block.seconds.start + sample * secondsPerSample;
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
        deClick_.reset();
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
        std::max({block.seconds.start, clip.span.startSeconds, event.span.startSeconds});
    const auto windowEnd =
        std::min({block.seconds.end, clip.span.endSeconds, event.span.endSeconds});
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

    // A clip that consumes its reading at a rate is fed on a grid of its own
    // rather than a block at a time, so that what the stretcher is handed is a
    // function of where the timeline is and never of how the callback was cut
    // up (renderThroughCells). Everything else here is the plain path: one
    // sample of reading per sample of output, where a block boundary already
    // changes nothing.
    //
    // Both answer the same question, which is whether this voice got everything
    // it asked for. What "everything" counts in differs: the plain path asks
    // the reader for a block's worth of samples, and the grid asks it for
    // whatever a cell consumes and then measures what it produced.
    const auto full = stretcher != nullptr
                          ? renderThroughCells(clip, event, block, stream, *stretcher, preRoll,
                                               scratch, region, windowStart, count)
                          : stream.read(readFrom, reading, wanted) == wanted;

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
    //
    // Except one, and it is the whole reason this is a condition rather than a
    // call. A voice starting at the beginning of what it plays is not starting
    // mid-material: there is nothing before it to be discontinuous with, and
    // what looks like a step is the material's own attack. De-clicking there
    // subtracts the attack and decays the correction over the ramp, so a clip
    // whose first sample is a transient loses it and gains 256 samples of tail.
    // The corpus found exactly that on an impulse sitting on a clip edge
    // (#2040), and the incumbent does not do it.
    //
    // A ramp longer than the block it starts in goes on into the next one. It
    // has to: clamping it to the block would make the same clip come out
    // differently at 128 samples a block and at 1024, and an offline render at
    // one size disagree with playback at another.
    const auto beginsAtItsOwnStart = windowStart <= event.span.startSeconds + (0.5 / sampleRate_);

    if (!sounded_ || !block.continuous) {
        if (beginsAtItsOwnStart)
            deClick_.reset();
        else
            deClick_.begin(region, clip.launchFadeSamples);
    } else {
        deClick_.advance(region);
    }

    out.getSubBlock(static_cast<std::size_t>(first), static_cast<std::size_t>(count)).add(region);

    // Silence the reader could not fill is not sounding, and a block it only
    // half filled ends in that silence as surely as one it missed entirely.
    // Either way the block that resumes starts mid-material, and saying this
    // voice carried on would leave it with nothing to take the step out of it.
    //
    // Against what was asked for rather than against the block: a stretched clip
    // consumes a different amount of reading than it renders, and a full block
    // built out of a short read is exactly the case this has to catch.
    sounded_ = full;
    return true;
}

}  // namespace magda::engine
