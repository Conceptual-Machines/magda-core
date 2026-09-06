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

    audio_.setSize(std::max(0, numChannels), std::max(0, samples));
    audio_.clear();

    writtenBy_.assign(static_cast<std::size_t>(std::max(0, samples)), 0);
    midi_.resize(
        static_cast<std::size_t>(midiCapacity > 0 ? midiCapacity : defaultMidiCapacity(window_)));
    midiWrites_.assign(midi_.size(), 0);
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

    // A stopped block covers no timeline, so what it returned belongs at no
    // position in the window.
    if (!block.playing || writtenBy_.empty())
        return;

    const auto windowSamples = static_cast<std::int64_t>(writtenBy_.size());
    const auto blockStart =
        std::llround((block.seconds.start - window_.startSeconds) * sampleRate_);

    // The part of the block the window holds.
    const auto from = std::max<std::int64_t>(blockStart, 0);
    const auto to = std::min<std::int64_t>(blockStart + block.numSamples, windowSamples);
    if (to <= from)
        return;

    // A return narrower than the capture leaves a channel nothing wrote, and
    // marking the span covered anyway would export that channel as silence.
    if (static_cast<int>(audio.getNumChannels()) < audio_.getNumChannels()) {
        narrowReturn_.store(true, std::memory_order_relaxed);
        return;
    }

    ++writeId_;

    const auto channels = audio_.getNumChannels();
    const auto count = static_cast<int>(to - from);
    const auto sourceOffset = static_cast<int>(from - blockStart);

    for (auto channel = 0; channel < channels; ++channel)
        audio_.copyFrom(channel, static_cast<int>(from),
                        audio.getChannelPointer(static_cast<std::size_t>(channel)) + sourceOffset,
                        count);

    markCovered(from, to);

    for (const auto metadata : midi) {
        // Three bytes and no more. A dropped message makes the capture
        // incomplete rather than silently thinner.
        if (metadata.numBytes < 1 || metadata.numBytes > 3) {
            midiOverflowed_.store(true, std::memory_order_relaxed);
            continue;
        }

        const auto at = blockStart + metadata.samplePosition;
        if (at < 0 || at >= windowSamples)
            continue;

        if (midiCount_ >= static_cast<int>(midi_.size())) {
            compactMidi();

            if (midiCount_ >= static_cast<int>(midi_.size())) {
                midiOverflowed_.store(true, std::memory_order_relaxed);
                break;
            }
        }

        midiWrites_[static_cast<std::size_t>(midiCount_)] = writeId_;
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
        auto& stamp = writtenBy_[static_cast<std::size_t>(at)];
        if (stamp == 0)
            ++added;
        stamp = writeId_;
    }

    if (added > 0)
        writtenSamples_.fetch_add(added, std::memory_order_relaxed);
}

void InsertCaptureSession::compactMidi() {
    auto kept = 0;

    for (auto at = 0; at < midiCount_; ++at) {
        const auto sample = static_cast<std::size_t>(midi_[static_cast<std::size_t>(at)].sample);
        if (writtenBy_[sample] != midiWrites_[static_cast<std::size_t>(at)])
            continue;

        midi_[static_cast<std::size_t>(kept)] = midi_[static_cast<std::size_t>(at)];
        midiWrites_[static_cast<std::size_t>(kept)] = midiWrites_[static_cast<std::size_t>(at)];
        ++kept;
    }

    midiCount_ = kept;
}

std::int64_t InsertCaptureSession::missingSamples() const {
    return static_cast<std::int64_t>(writtenBy_.size()) -
           writtenSamples_.load(std::memory_order_relaxed);
}

InsertCapture InsertCaptureSession::take() const {
    InsertCapture::Contents contents;
    contents.audio.makeCopyOf(audio_);

    // Only what the write that still owns its position put there: a pass that
    // went over a stretch again replaced its audio, and its messages with it.
    contents.midi.reserve(static_cast<std::size_t>(midiCount_));
    for (auto at = 0; at < midiCount_; ++at) {
        const auto& event = midi_[static_cast<std::size_t>(at)];
        if (writtenBy_[static_cast<std::size_t>(event.sample)] ==
            midiWrites_[static_cast<std::size_t>(at)])
            contents.midi.push_back(event);
    }
    contents.window = window_;
    contents.sampleRate = sampleRate_;
    contents.roundTripSeconds =
        sampleRate_ > 0.0 ? static_cast<double>(live_.latencySamples()) / sampleRate_ : 0.0;
    contents.complete = !writtenBy_.empty() && missingSamples() == 0 &&
                        !midiOverflowed_.load(std::memory_order_relaxed) &&
                        !narrowReturn_.load(std::memory_order_relaxed);

    // A pass that looped wrote later blocks at earlier positions; readers walk
    // this forwards.
    std::sort(contents.midi.begin(), contents.midi.end(),
              [](const CapturedMidiEvent& first, const CapturedMidiEvent& second) {
                  return first.sample < second.sample;
              });

    return InsertCapture(std::move(contents));
}

}  // namespace magda::engine
