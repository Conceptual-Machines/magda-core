#include "io/CaptureRing.hpp"

#include <algorithm>
#include <cmath>

#include "transport/TempoMap.hpp"

namespace magda::engine {

namespace {

/// Frames a block may be writing while a reader copies: more than any callback delivers.
constexpr std::int64_t kWriteMargin = 16384;

/// The shortest block a stamp is budgeted for. Shorter ones only shorten the history.
constexpr std::int64_t kFramesPerStamp = 16;

constexpr double kBeatTolerance = 1.0e-6;

bool continues(const CaptureStamp& before, const CaptureStamp& after) {
    return before.playing && before.frame + before.numSamples == after.frame &&
           std::abs(before.beats.end - after.beats.start) < kBeatTolerance;
}

}  // namespace

std::optional<CaptureRun> latestPass(const std::vector<CaptureStamp>& stamps) {
    auto last = std::find_if(stamps.rbegin(), stamps.rend(),
                             [](const CaptureStamp& stamp) { return stamp.playing; });
    if (last == stamps.rend())
        return std::nullopt;

    auto first = last;
    while (std::next(first) != stamps.rend() && continues(*std::next(first), *first))
        ++first;

    return CaptureRun{.beats = {first->beats.start, last->beats.end},
                      .firstFrame = first->frame,
                      .frames = last->frame + last->numSamples - first->frame};
}

std::int64_t frameAt(const std::vector<CaptureStamp>& stamps, const CaptureRun& run, double beat) {
    beat = std::clamp(beat, run.beats.start, run.beats.end);
    for (const auto& stamp : stamps) {
        if (stamp.frame < run.firstFrame || stamp.frame >= run.firstFrame + run.frames ||
            beat > stamp.beats.end)
            continue;
        const auto length = stamp.beats.length();
        const auto fraction =
            length > 0.0 ? std::clamp((beat - stamp.beats.start) / length, 0.0, 1.0) : 0.0;
        return stamp.frame + std::llround(fraction * stamp.numSamples);
    }
    return run.firstFrame + run.frames;
}

double barsBefore(const TempoMap& map, double endBeat, int bars) {
    // Just inside, so a beat on a bar line counts the bar before it.
    auto beat = endBeat;
    for (int bar = 0; bar < std::max(1, bars); ++bar) {
        const auto inside = beat - kBeatTolerance;
        const auto where = map.barsAndBeatsAt(inside);
        beat = inside - where.beat * 4.0 / where.denominator;
    }
    return beat;
}

CaptureRing::CaptureRing(int numChannels, std::int64_t capacityFrames)
    : numChannels_(std::max(1, numChannels)),
      capacity_(std::max<std::int64_t>(capacityFrames, kWriteMargin * 2)),
      stamps_(
          std::make_unique<Stamp[]>(static_cast<std::size_t>(capacity_ / kFramesPerStamp + 64))),
      stampCapacity_(capacity_ / kFramesPerStamp + 64) {
    for (int channel = 0; channel < numChannels_; ++channel)
        channels_.push_back(
            std::make_unique<std::atomic<float>[]>(static_cast<std::size_t>(capacity_)));
}

void CaptureRing::write(const juce::AudioBuffer<float>& block, BeatRange beats, bool playing) {
    const auto frames = block.getNumSamples();
    if (frames <= 0 || block.getNumChannels() <= 0)
        return;

    const auto start = written_.load(std::memory_order_relaxed);
    for (int channel = 0; channel < numChannels_; ++channel) {
        const auto* source = block.getReadPointer(std::min(channel, block.getNumChannels() - 1));
        auto* ring = channels_[static_cast<std::size_t>(channel)].get();
        auto slot = start % capacity_;
        for (int i = 0; i < frames; ++i) {
            ring[slot].store(source[i], std::memory_order_relaxed);
            if (++slot == capacity_)
                slot = 0;
        }
    }

    const auto count = stampCount_.load(std::memory_order_relaxed);
    auto& stamp = stamps_[static_cast<std::size_t>(count % stampCapacity_)];
    stamp.frame.store(start, std::memory_order_relaxed);
    stamp.numSamples.store(frames, std::memory_order_relaxed);
    stamp.beatStart.store(beats.start, std::memory_order_relaxed);
    stamp.beatEnd.store(beats.end, std::memory_order_relaxed);
    stamp.playing.store(playing, std::memory_order_relaxed);

    stampCount_.store(count + 1, std::memory_order_release);
    written_.store(start + frames, std::memory_order_release);
}

std::int64_t CaptureRing::oldestKept(std::int64_t written, std::int64_t margin) const {
    return std::max<std::int64_t>(0, written + margin - capacity_);
}

std::vector<CaptureStamp> CaptureRing::stamps() const {
    const auto count = stampCount_.load(std::memory_order_acquire);
    const auto first = std::max<std::int64_t>(0, count - stampCapacity_ + 1);

    std::vector<CaptureStamp> out;
    out.reserve(static_cast<std::size_t>(count - first));
    for (auto index = first; index < count; ++index) {
        const auto& stamp = stamps_[static_cast<std::size_t>(index % stampCapacity_)];
        out.push_back({.frame = stamp.frame.load(std::memory_order_relaxed),
                       .numSamples = stamp.numSamples.load(std::memory_order_relaxed),
                       .beats = {stamp.beatStart.load(std::memory_order_relaxed),
                                 stamp.beatEnd.load(std::memory_order_relaxed)},
                       .playing = stamp.playing.load(std::memory_order_relaxed)});
    }

    // A slot the writer reached during the copy, and any block whose audio has gone.
    const auto after = stampCount_.load(std::memory_order_acquire);
    const auto overwritten = std::max<std::int64_t>(0, after - stampCapacity_ + 1 - first);
    out.erase(out.begin(), out.begin() + std::min<std::int64_t>(overwritten, out.size()));
    const auto kept = oldestKept(written_.load(std::memory_order_acquire), kWriteMargin);
    std::erase_if(out, [kept](const CaptureStamp& stamp) { return stamp.frame < kept; });
    return out;
}

bool CaptureRing::read(std::int64_t firstFrame, std::int64_t frames,
                       juce::AudioBuffer<float>& out) const {
    const auto written = written_.load(std::memory_order_acquire);
    if (frames <= 0 || firstFrame < oldestKept(written, kWriteMargin) ||
        firstFrame + frames > written)
        return false;

    out.setSize(numChannels_, static_cast<int>(frames), false, false, true);
    for (int channel = 0; channel < numChannels_; ++channel) {
        const auto* ring = channels_[static_cast<std::size_t>(channel)].get();
        auto* dest = out.getWritePointer(channel);
        auto slot = firstFrame % capacity_;
        for (std::int64_t i = 0; i < frames; ++i) {
            dest[i] = ring[slot].load(std::memory_order_relaxed);
            if (++slot == capacity_)
                slot = 0;
        }
    }

    // The writer may have lapped the oldest frames while they were copied.
    return firstFrame >= oldestKept(written_.load(std::memory_order_acquire), kWriteMargin);
}

}  // namespace magda::engine
