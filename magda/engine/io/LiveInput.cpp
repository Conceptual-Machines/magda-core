#include "io/LiveInput.hpp"

#include <algorithm>

namespace magda::engine {

namespace {

/// What one event costs against a port's byte budget: MidiBuffer stores a
/// sample position and a length in front of the message itself.
constexpr int kEventOverheadBytes = 6;

}  // namespace

void LiveInputFeed::beginCallback(const LiveInputBlock& input, int numSamples) {
    callbackAudio_ = input.audio;
    midi_ = input.midi;

    const auto delivered = static_cast<int>(callbackAudio_.getNumSamples());
    const auto silent = callbackAudio_.getNumChannels() == 0;

    // A host that delivered fewer input samples than it asked to render is a
    // host bug, and the blocks past what it delivered read silence rather than
    // whatever is behind the pointer.
    const bool enough = silent || delivered >= numSamples;
    jassert(enough);
    juce::ignoreUnused(enough);

    callbackSamples_ = silent ? 0 : std::min(numSamples, delivered);

    beginSegment(0, 0);
}

void LiveInputFeed::beginSegment(int startSample, int numSamples) {
    segmentStart_ = startSample;
    segmentSamples_ = numSamples;

    if (startSample < 0 || numSamples <= 0 || startSample + numSamples > callbackSamples_) {
        audio_ = {};
        return;
    }

    audio_ = callbackAudio_.getSubBlock(static_cast<std::size_t>(startSample),
                                        static_cast<std::size_t>(numSamples));
}

void LiveInputFeed::endCallback() {
    callbackAudio_ = {};
    audio_ = {};
    midi_ = {};
    callbackSamples_ = 0;
    segmentStart_ = 0;
    segmentSamples_ = 0;
}

int LiveInputFeed::appendEvents(LiveMidiSourceId source, juce::MidiBuffer& out,
                                int maxBytes) const {
    if (segmentSamples_ <= 0)
        return 0;

    const auto end = segmentStart_ + segmentSamples_;
    int dropped = 0;

    for (const auto& stream : midi_) {
        if (stream.events == nullptr)
            continue;
        if (source != kAnyLiveMidiSource && stream.source != source)
            continue;

        for (const auto metadata : *stream.events) {
            if (metadata.samplePosition < segmentStart_ || metadata.samplePosition >= end)
                continue;

            if (static_cast<int>(out.data.size()) + kEventOverheadBytes + metadata.numBytes >
                maxBytes) {
                ++dropped;
                continue;
            }

            out.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition - segmentStart_);
        }
    }

    return dropped;
}

LiveAudioInput::LiveAudioInput(const LiveInputFeed& feed, std::span<const int> channels,
                               int latencySamples)
    : feed_(feed), channels_(channels.begin(), channels.end()), latencySamples_(latencySamples) {}

void LiveAudioInput::render(const BlockInfo& /*block*/, juce::dsp::AudioBlock<float> out) {
    const auto in = feed_.audio();
    const auto numSamples = out.getNumSamples();
    const auto available = in.getNumChannels();

    if (channels_.empty() || available == 0 || in.getNumSamples() < numSamples) {
        out.clear();
        if (!channels_.empty())
            missingChannels_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    bool missing = false;

    for (std::size_t c = 0; c < out.getNumChannels(); ++c) {
        // A destination wider than the map repeats the map's last channel, so
        // a mono input fills both ears.
        const auto index = channels_[std::min(c, channels_.size() - 1)];

        if (index < 0 || static_cast<std::size_t>(index) >= available) {
            out.getSingleChannelBlock(c).clear();
            missing = true;
            continue;
        }

        out.getSingleChannelBlock(c).copyFrom(
            in.getSingleChannelBlock(static_cast<std::size_t>(index)).getSubBlock(0, numSamples));
    }

    if (missing)
        missingChannels_.fetch_add(1, std::memory_order_relaxed);
}

LiveMidiInput::LiveMidiInput(const LiveInputFeed& feed, LiveMidiSourceId source, int latencySamples)
    : feed_(feed), source_(source), latencySamples_(latencySamples) {}

void LiveMidiInput::render(const BlockInfo& /*block*/, juce::MidiBuffer& out) {
    if (const auto dropped = feed_.appendEvents(source_, out, kMaxMidiBytesPerPort); dropped > 0)
        dropped_.fetch_add(static_cast<std::uint32_t>(dropped), std::memory_order_relaxed);
}

}  // namespace magda::engine
