#include "io/CaptureRing.hpp"

#include <algorithm>
#include <cmath>

#include "transport/TempoMap.hpp"

namespace magda::engine {

namespace {

/// Frames a block may be writing while a reader copies: more than any callback delivers.
constexpr std::int64_t kWriteMargin = 16384;

/// Frames per peak: five minutes at 48 kHz is about 14000 of them.
constexpr std::int64_t kFramesPerPeak = 1024;

/// Below this a chunk is silence (-60 dB), and this much of it ends a phrase.
constexpr float kSilence = 0.001f;
constexpr double kPhraseGapSeconds = 3.0;

constexpr double kBeatTolerance = 1.0e-6;

/// Live MIDI messages kept: hours of playing, a second of a dense controller sweep.
constexpr std::int64_t kMidiCapacity = 1 << 17;

std::uint64_t packMidi(LiveMidiSourceId source, const std::uint8_t* data, int size) {
    std::uint64_t word = static_cast<std::uint32_t>(source);
    word = (word << 8) | static_cast<std::uint64_t>(size);
    for (int i = 0; i < 3; ++i)
        word = (word << 8) | (i < size ? data[i] : 0u);
    return word;
}

}  // namespace

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

CaptureRing::CaptureRing(int numChannels, std::int64_t capacityFrames, double sampleRate)
    : numChannels_(std::max(1, numChannels)),
      capacity_(std::max<std::int64_t>(capacityFrames, kWriteMargin * 2)),
      gapFrames_(std::llround(kPhraseGapSeconds * std::max(1.0, sampleRate))),
      peaks_(std::make_unique<std::atomic<float>[]>(
          static_cast<std::size_t>(capacity_ / kFramesPerPeak + 1))),
      peakSlots_(capacity_ / kFramesPerPeak + 1),
      midiFrames_(std::make_unique<std::atomic<std::int64_t>[]>(kMidiCapacity)),
      midiWords_(std::make_unique<std::atomic<std::uint64_t>[]>(kMidiCapacity)),
      midiCapacity_(kMidiCapacity) {
    for (int channel = 0; channel < numChannels_; ++channel)
        channels_.push_back(
            std::make_unique<std::atomic<float>[]>(static_cast<std::size_t>(capacity_)));
}

void CaptureRing::notePhrase(std::int64_t chunkStart, std::int64_t chunkEnd) {
    // A new phrase after a long enough silence, or after the transport stopped.
    const auto end = phraseEnd_.load(std::memory_order_relaxed);
    if (end < 0 || chunkStart - end >= gapFrames_ || end <= stoppedAt_)
        phraseStart_.store(std::max(chunkStart, stoppedAt_), std::memory_order_relaxed);
    phraseEnd_.store(chunkEnd, std::memory_order_relaxed);
}

void CaptureRing::write(const juce::AudioBuffer<float>& block, BeatRange beats, bool playing) {
    const auto frames = block.getNumSamples();
    if (frames <= 0 || block.getNumChannels() <= 0)
        return;

    const auto start = written_.load(std::memory_order_relaxed);
    if (playing) {
        if (!lastPlaying_ || std::abs(lastBeatEnd_ - beats.start) >= kBeatTolerance) {
            passStart_.store(start, std::memory_order_relaxed);
            passStartBeat_.store(beats.start, std::memory_order_relaxed);
        }
        passEnd_.store(start + frames, std::memory_order_relaxed);
        passEndBeat_.store(beats.end, std::memory_order_relaxed);
    } else if (lastPlaying_) {
        // What still sounds past a stop is the pass's tail, not something played.
        stoppedAt_ = start;
        awaitingSilence_ = true;
    }
    lastPlaying_ = playing;
    lastBeatEnd_ = beats.end;

    for (int i = 0; i < frames; ++i) {
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            chunkPeak_ = std::max(chunkPeak_, std::abs(block.getSample(channel, i)));
        if (const auto frame = start + i; (frame + 1) % kFramesPerPeak == 0) {
            peaks_[static_cast<std::size_t>((frame / kFramesPerPeak) % peakSlots_)].store(
                chunkPeak_, std::memory_order_relaxed);
            if (!playing && chunkPeak_ <= kSilence)
                awaitingSilence_ = false;
            else if (!playing && !awaitingSilence_)
                notePhrase(frame + 1 - kFramesPerPeak, frame + 1);
            chunkPeak_ = 0.0f;
        }
    }
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

    written_.store(start + frames, std::memory_order_release);
}

void CaptureRing::writeMidi(std::span<const LiveMidiStream> streams, int segmentStart,
                            int numSamples) {
    const auto base = written_.load(std::memory_order_relaxed);
    auto count = midiCount_.load(std::memory_order_relaxed);
    for (const auto& stream : streams) {
        if (stream.events == nullptr)
            continue;
        for (const auto metadata : *stream.events) {
            const auto offset = metadata.samplePosition - segmentStart;
            if (offset < 0 || offset >= numSamples || metadata.numBytes < 1 ||
                metadata.numBytes > 3)
                continue;
            const auto slot = static_cast<std::size_t>(count % midiCapacity_);
            midiFrames_[slot].store(base + offset, std::memory_order_relaxed);
            midiWords_[slot].store(packMidi(stream.source, metadata.data, metadata.numBytes),
                                   std::memory_order_relaxed);
            ++count;
        }
    }
    midiCount_.store(count, std::memory_order_release);
}

std::vector<CaptureMidiEvent> CaptureRing::midiFrom(std::int64_t firstFrame) const {
    const auto count = midiCount_.load(std::memory_order_acquire);
    const auto oldest = std::max<std::int64_t>(0, count - midiCapacity_ + 1);

    std::vector<CaptureMidiEvent> out;
    for (auto index = oldest; index < count; ++index) {
        const auto slot = static_cast<std::size_t>(index % midiCapacity_);
        const auto frame = midiFrames_[slot].load(std::memory_order_relaxed);
        if (frame < firstFrame)
            continue;
        const auto word = midiWords_[slot].load(std::memory_order_relaxed);
        CaptureMidiEvent event{.frame = frame,
                               .source = static_cast<LiveMidiSourceId>(word >> 32),
                               .size = static_cast<std::uint8_t>((word >> 24) & 0xff)};
        for (int i = 0; i < 3; ++i)
            event.bytes[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((word >> (16 - 8 * i)) & 0xff);
        out.push_back(event);
    }

    // Slots the writer reached during the copy are no longer what was read.
    const auto lapped = midiCount_.load(std::memory_order_acquire) - midiCapacity_ + 1 - oldest;
    if (lapped > 0)
        out.erase(out.begin(), out.begin() + std::min<std::int64_t>(lapped, out.size()));
    return out;
}

std::int64_t CaptureRing::oldestKept(std::int64_t written, std::int64_t margin) const {
    return std::max<std::int64_t>(0, written + margin - capacity_);
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

CaptureMeter CaptureRing::meter(int slices) const {
    const auto written = written_.load(std::memory_order_acquire);
    CaptureMeter out{.writtenFrames = written,
                     .capacityFrames = capacity_,
                     .oldestReadable = oldestKept(written, kWriteMargin)};

    // The ring as a fixed window ending now, so the newest audio is always at the right.
    out.peaks = peaks(written - capacity_, written, slices);

    const auto clamp = [&](std::int64_t start, std::int64_t end) {
        return start < 0 ? CaptureSpan{}
                         : CaptureSpan{std::max(start, out.oldestReadable), std::min(end, written)};
    };
    out.pass =
        clamp(passStart_.load(std::memory_order_relaxed), passEnd_.load(std::memory_order_relaxed));
    out.passBeats = {passStartBeat_.load(std::memory_order_relaxed),
                     passEndBeat_.load(std::memory_order_relaxed)};
    out.phrase = clamp(phraseStart_.load(std::memory_order_relaxed),
                       phraseEnd_.load(std::memory_order_relaxed));
    return out;
}

std::vector<float> CaptureRing::peaks(std::int64_t first, std::int64_t end, int slices) const {
    slices = std::max(1, slices);
    std::vector<float> out(static_cast<std::size_t>(slices), 0.0f);
    const auto written = written_.load(std::memory_order_acquire);
    const auto oldest = oldestKept(written, kWriteMargin);
    const auto span = std::max<std::int64_t>(1, end - first);

    // Closer than a chunk a slice: the samples, from what is still there to read.
    if (span / slices < kFramesPerPeak) {
        for (int slice = 0; slice < slices; ++slice) {
            const auto from = std::max(oldest, first + span * slice / slices);
            const auto to = std::min(written, first + span * (slice + 1) / slices);
            auto peak = 0.0f;
            for (auto frame = from; frame < to; ++frame)
                for (const auto& channel : channels_)
                    peak = std::max(
                        peak, std::abs(channel[static_cast<std::size_t>(frame % capacity_)].load(
                                  std::memory_order_relaxed)));
            out[static_cast<std::size_t>(slice)] = peak;
        }
        return out;
    }

    const auto completeChunks = written / kFramesPerPeak;
    for (int slice = 0; slice < slices; ++slice) {
        const auto from = std::max(oldest, first + span * slice / slices);
        const auto to = first + span * (slice + 1) / slices;
        auto peak = 0.0f;
        for (auto chunk = from / kFramesPerPeak;
             chunk * kFramesPerPeak < to && chunk < completeChunks; ++chunk)
            peak = std::max(peak, peaks_[static_cast<std::size_t>(chunk % peakSlots_)].load(
                                      std::memory_order_relaxed));
        out[static_cast<std::size_t>(slice)] = peak;
    }
    return out;
}

}  // namespace magda::engine
