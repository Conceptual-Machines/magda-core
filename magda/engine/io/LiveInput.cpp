#include "io/LiveInput.hpp"

#include <algorithm>

namespace magda::engine {

namespace {

/// What one event costs against a port's byte budget: MidiBuffer stores a
/// sample position and a length in front of the message itself.
constexpr int kEventOverheadBytes = 6;

}  // namespace

void LiveInputFeed::prepare(int maxChannels, int maxBlockSize) {
    const auto channels = std::max(scratch_.getNumChannels(), std::max(0, maxChannels));
    const auto samples = std::max(scratch_.getNumSamples(), std::max(0, maxBlockSize));

    if (channels != scratch_.getNumChannels() || samples != scratch_.getNumSamples())
        scratch_.setSize(channels, samples, false, true, false);
}

void LiveInputFeed::beginCallback(const LiveInputBlock& input, int numSamples) {
    midi_ = input.midi;

    const auto delivered = static_cast<int>(input.audio.getNumSamples());
    const auto deliveredChannels = static_cast<int>(input.audio.getNumChannels());
    const auto wanted = std::min(numSamples, delivered);

    const auto channels = std::min(deliveredChannels, scratch_.getNumChannels());
    const auto samples = std::min(wanted, scratch_.getNumSamples());

    // A host that delivered fewer input samples than it asked to render is a
    // host bug; what it did deliver is copied, and the rest of the block is
    // silence rather than whatever is behind the pointer.
    const bool enough = deliveredChannels == 0 || delivered >= numSamples;
    jassert(enough);
    juce::ignoreUnused(enough);

    if (deliveredChannels > 0 && (channels < deliveredChannels || samples < wanted))
        unfit_.fetch_add(1, std::memory_order_relaxed);

    // The copy, and the reason this holds a buffer rather than the caller's
    // pointers: that memory may be the output buffer, which is cleared and
    // written before any input op runs.
    for (auto channel = 0; channel < channels; ++channel)
        scratch_.copyFrom(
            channel, 0, input.audio.getChannelPointer(static_cast<std::size_t>(channel)), samples);

    callbackChannels_ = samples > 0 ? channels : 0;
    callbackSamples_ = callbackChannels_ > 0 ? samples : 0;

    beginSegment(0, 0);
}

void LiveInputFeed::beginSegment(int startSample, int numSamples) {
    segmentStart_ = startSample;
    segmentSamples_ = numSamples;

    if (callbackChannels_ <= 0 || startSample < 0 || numSamples <= 0 ||
        startSample + numSamples > callbackSamples_) {
        audio_ = {};
        return;
    }

    audio_ = juce::dsp::AudioBlock<const float>(scratch_)
                 .getSubsetChannelBlock(0, static_cast<std::size_t>(callbackChannels_))
                 .getSubBlock(static_cast<std::size_t>(startSample),
                              static_cast<std::size_t>(numSamples));
}

void LiveInputFeed::endCallback() {
    audio_ = {};
    midi_ = {};
    callbackChannels_ = 0;
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
