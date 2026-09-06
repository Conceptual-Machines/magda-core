#include "insert/InsertCaptureSession.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

namespace {

/// Past what a DIN return can carry in a second (see the header).
constexpr double kMidiEventsPerSecond = 1100.0;

/// Room for a window too short for the rate above to say anything useful.
constexpr int kMinimumMidiCapacity = 64;

}  // namespace

int InsertCaptureSession::defaultMidiCapacity(const CaptureWindow& window) {
    const auto events = std::ceil(window.lengthSeconds() * kMidiEventsPerSecond);
    return std::max(kMinimumMidiCapacity, static_cast<int>(events) + kMinimumMidiCapacity);
}

InsertCaptureSession::InsertCaptureSession(EngineInsert& live, const CaptureWindow& window,
                                           double sampleRate, int numChannels, int midiCapacity)
    : live_(live), window_(window), sampleRate_(sampleRate) {
    const auto samples =
        static_cast<int>(std::llround(window_.lengthSeconds() * std::max(0.0, sampleRate)));

    audio_.setSize(std::max(1, numChannels), std::max(0, samples));
    audio_.clear();

    covered_.assign(static_cast<std::size_t>(std::max(0, samples)), false);
    midi_.resize(
        static_cast<std::size_t>(midiCapacity > 0 ? midiCapacity : defaultMidiCapacity(window_)));
}

void InsertCaptureSession::prepare(const RenderContext& context) {
    live_.prepare(context);
}

void InsertCaptureSession::reset() {
    live_.reset();
}

int InsertCaptureSession::latencySamples() const {
    return live_.latencySamples();
}

void InsertCaptureSession::send(const BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
                                const juce::MidiBuffer& midi) {
    live_.send(block, audio, midi);
}

void InsertCaptureSession::receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                                   juce::MidiBuffer& midi) {
    live_.receive(block, audio, midi);

    // A stopped block covers no timeline, so there is nowhere in the window to
    // put what it returned. The graph still ran and the hardware still spoke;
    // what it said belongs to no instant.
    if (!block.playing || covered_.empty())
        return;

    const auto windowSamples = static_cast<std::int64_t>(covered_.size());
    const auto blockStart =
        std::llround((block.seconds.start - window_.startSeconds) * sampleRate_);

    // The part of the block the window holds, which is all of it for every
    // block of a pass that stays inside the range.
    const auto from = std::max<std::int64_t>(blockStart, 0);
    const auto to = std::min<std::int64_t>(blockStart + block.numSamples, windowSamples);
    if (to <= from)
        return;

    const auto channels =
        std::min(static_cast<int>(audio.getNumChannels()), audio_.getNumChannels());
    const auto count = static_cast<int>(to - from);
    const auto sourceOffset = static_cast<int>(from - blockStart);

    for (auto channel = 0; channel < channels; ++channel)
        audio_.copyFrom(channel, static_cast<int>(from),
                        audio.getChannelPointer(static_cast<std::size_t>(channel)) + sourceOffset,
                        count);

    markCovered(from, to);

    for (const auto metadata : midi) {
        // Three bytes and no more, which is the whole of what a hardware return
        // speaks in. A longer message is dropped, and a capture that dropped one
        // is not complete.
        if (metadata.numBytes < 1 || metadata.numBytes > 3) {
            midiOverflowed_.store(true, std::memory_order_relaxed);
            continue;
        }

        const auto at = blockStart + metadata.samplePosition;
        if (at < 0 || at >= windowSamples)
            continue;

        if (midiCount_ >= static_cast<int>(midi_.size())) {
            midiOverflowed_.store(true, std::memory_order_relaxed);
            break;
        }

        auto& event = midi_[static_cast<std::size_t>(midiCount_)];
        event.sample = at;
        event.numBytes = static_cast<std::uint8_t>(metadata.numBytes);
        event.status = metadata.data[0];
        event.data1 = metadata.numBytes > 1 ? metadata.data[1] : 0;
        event.data2 = metadata.numBytes > 2 ? metadata.data[2] : 0;
        ++midiCount_;
    }
}

void InsertCaptureSession::markCovered(std::int64_t from, std::int64_t to) {
    std::int64_t added = 0;

    for (auto at = from; at < to; ++at) {
        auto covered = covered_[static_cast<std::size_t>(at)];
        if (!covered) {
            covered_[static_cast<std::size_t>(at)] = true;
            ++added;
        }
    }

    if (added > 0)
        writtenSamples_.fetch_add(added, std::memory_order_relaxed);
}

std::int64_t InsertCaptureSession::missingSamples() const {
    return static_cast<std::int64_t>(covered_.size()) -
           writtenSamples_.load(std::memory_order_relaxed);
}

InsertCapture InsertCaptureSession::take() const {
    InsertCapture::Contents contents;
    contents.audio.makeCopyOf(audio_);
    contents.midi.assign(midi_.begin(), midi_.begin() + midiCount_);
    contents.window = window_;
    contents.sampleRate = sampleRate_;
    contents.roundTripSeconds =
        sampleRate_ > 0.0 ? static_cast<double>(live_.latencySamples()) / sampleRate_ : 0.0;
    contents.complete = !covered_.empty() && missingSamples() == 0 &&
                        !midiOverflowed_.load(std::memory_order_relaxed);

    // Sorted, because a pass that looped wrote later blocks over earlier
    // positions and a reader walks this forwards.
    std::sort(contents.midi.begin(), contents.midi.end(),
              [](const CapturedMidiEvent& first, const CapturedMidiEvent& second) {
                  return first.sample < second.sample;
              });

    return InsertCapture(std::move(contents));
}

}  // namespace magda::engine
