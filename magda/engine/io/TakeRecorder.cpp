#include "io/TakeRecorder.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace magda::engine {

namespace {

/// A wrap anchors the cursor exactly on the loop start, so anything further off
/// than this is a locate rather than a pass boundary.
constexpr double kLoopStartTolerance = 1.0e-6;

/// How short the final pass has to be to count as a stop rather than a take.
constexpr double kFullPassFraction = 0.95;

int takeChannels(const TakeRecorderSettings& settings) {
    return std::max<int>(1, static_cast<int>(settings.channels.size()));
}

RenderContext fileContext(const RenderContext& context, const TakeRecorderSettings& settings) {
    return {.sampleRate = context.sampleRate,
            .maxBlockSize = context.maxBlockSize,
            .numChannels = takeChannels(settings)};
}

RecordStreamSettings queueFor(const TakeRecorderSettings& settings) {
    auto queue = settings.stream;
    queue.numChannels = takeChannels(settings);
    return queue;
}

bool atLoopStart(const BlockInfo& block, const LoopRange& loop) {
    return loop.valid() && std::abs(block.beats.start - loop.startBeat) <= kLoopStartTolerance;
}

/// The last full pass, stepping back one if the final pass was cut short. Only
/// the last one can be, which is what makes the rule this simple.
std::size_t activeTake(std::span<const RecordedPass> passes) {
    auto longest = 0.0;
    for (const auto& pass : passes)
        longest = std::max(longest, pass.durationSeconds);

    const auto last = passes.size() - 1;
    if (passes.size() > 1 && passes[last].durationSeconds < longest * kFullPassFraction)
        return last - 1;

    return last;
}

}  // namespace

TakeRecorder::TakeRecorder(const LiveInputFeed& feed, const RenderContext& context,
                           TakeRecorderSettings settings)
    : settings_(std::move(settings)),
      input_(feed, settings_.channels, settings_.latencySamples),
      sink_(settings_.directory, settings_.name, settings_.file, fileContext(context, settings_)),
      stream_(sink_, queueFor(settings_)) {
    scratch_.setSize(takeChannels(settings_), std::max(1, context.maxBlockSize), false, true,
                     false);

    if (settings_.latencySamples < 0)
        pad_.setSize(takeChannels(settings_), -settings_.latencySamples, false, true, false);

    latencySeconds_ = context.sampleRate > 0.0
                          ? static_cast<double>(settings_.latencySamples) / context.sampleRate
                          : 0.0;
}

void TakeRecorder::capture(const BlockInfo& block, bool countingIn, const LoopRange& loop) {
    if (state_ == State::stopped)
        return;

    // A count-in is time before the play position and a stop is where a take
    // ends, so neither is part of one.
    if (!block.playing || countingIn) {
        if (state_ == State::rolling)
            stop();

        return;
    }

    if (state_ == State::waiting) {
        start(block, loop);
    } else if (!block.continuous) {
        if (!atLoopStart(block, loop)) {
            stop();
            return;
        }

        openPass(loop);
    }

    write(block);

    // What a take holds is what happened, and what happened at the end of this
    // block arrives a latency later: the tail runs on by exactly as much as the
    // head gave up.
    lastBeat_ = block.beatAtTime(block.seconds.end - latencySeconds_);
}

void TakeRecorder::start(const BlockInfo& block, const LoopRange& loop) {
    state_ = State::rolling;
    rolling_.store(true, std::memory_order_relaxed);

    startBeat_ = block.beats.start;
    startedAtLoopStart_ = atLoopStart(block, loop);
    headDrop_ = std::max(0, settings_.latencySamples);

    // A negative latency asks for samples from before the take was armed.
    // Silence stands in for them, which is what leaves the first real sample
    // where it happened.
    if (const auto padded = pad_.getNumSamples(); padded > 0) {
        stream_.writeAudio(juce::dsp::AudioBlock<const float>(std::as_const(pad_)), padded);
        written_ += padded;
        captured_.store(written_, std::memory_order_relaxed);
    }
}

void TakeRecorder::openPass(const LoopRange& loop) {
    wrapped_ = true;
    loopStartBeat_ = loop.startBeat;
    loopEndBeat_ = loop.endBeat;

    // The samples the wrap belongs to are still a latency away. A negative one
    // puts that boundary in the past, where the only thing left is to end the
    // pass here and be late by the adjustment.
    pendingSplit_ = std::max(0, settings_.latencySamples);
}

void TakeRecorder::stop() {
    state_ = State::stopped;
    rolling_.store(false, std::memory_order_relaxed);
}

void TakeRecorder::write(const BlockInfo& block) {
    const auto numSamples = std::min(block.numSamples, scratch_.getNumSamples());
    if (numSamples <= 0)
        return;

    input_.render(block, juce::dsp::AudioBlock<float>(scratch_).getSubBlock(
                             0, static_cast<std::size_t>(numSamples)));

    const auto captured = juce::dsp::AudioBlock<const float>(std::as_const(scratch_));

    auto offset = 0;
    if (headDrop_ > 0) {
        const auto dropped = std::min(headDrop_, numSamples);
        headDrop_ -= dropped;
        offset += dropped;
    }

    for (;;) {
        if (pendingSplit_ == 0) {
            sink_.markPassEnd(written_);
            pendingSplit_ = -1;
        }

        if (offset >= numSamples)
            break;

        auto chunk = numSamples - offset;
        if (pendingSplit_ > 0)
            chunk = std::min(chunk, pendingSplit_);

        stream_.writeAudio(
            captured.getSubBlock(static_cast<std::size_t>(offset), static_cast<std::size_t>(chunk)),
            chunk);
        written_ += chunk;

        if (pendingSplit_ > 0)
            pendingSplit_ -= chunk;

        offset += chunk;
    }

    captured_.store(written_, std::memory_order_relaxed);
}

RecordedTake TakeRecorder::finish() {
    if (finished_)
        return result_;

    finished_ = true;
    stop();
    stream_.finish();

    result_.samplesLost = stream_.samplesLost();
    result_.failed = stream_.failed() || sink_.failed();

    auto passes = sink_.passes();

    // A first pass that did not begin on the loop start is a lead-in: with no
    // offset of its own it cannot share the clip start the others do, and a
    // wrap having happened at all is what makes it a lead-in rather than the
    // whole recording.
    if (passes.size() > 1 && !startedAtLoopStart_) {
        passes.front().file.deleteFile();
        passes = passes.subspan(1);
    }

    if (!passes.empty()) {
        const auto active = activeTake(passes);
        result_.file = passes[active].file;

        // One pass is an ordinary clip, and the model keeps `takes` empty for
        // one of those: they are loop-record alternatives or nothing.
        if (passes.size() > 1) {
            for (const auto& pass : passes)
                result_.clip.takes.push_back(AudioTake{
                    .filePath = pass.file.getFullPathName(),
                    .durationSeconds = pass.durationSeconds,
                });

            result_.clip.currentTakeIndex = static_cast<int>(active);
        }
    }

    placeClip(result_);
    return result_;
}

void TakeRecorder::placeClip(RecordedTake& take) const {
    // Loop-aligned the moment a wrap happened: every pass that survived covers
    // the loop, so that is where the clip goes and how long it is.
    if (wrapped_) {
        take.startBeat = loopStartBeat_;
        take.lengthBeats = std::max(0.0, loopEndBeat_ - loopStartBeat_);
        return;
    }

    take.startBeat = startBeat_;
    take.lengthBeats = std::max(0.0, lastBeat_ - startBeat_);
}

}  // namespace magda::engine
