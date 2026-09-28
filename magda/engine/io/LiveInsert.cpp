#include "io/LiveInsert.hpp"

#include <algorithm>

namespace magda::engine {

LiveInsert::LiveInsert(const LiveInputFeed& inputs, LiveOutputFeed& outputs, LiveInsertRoute route)
    : outputs_(outputs), route_(std::move(route)), return_(inputs, route_.returnChannels) {}

LiveInsert::~LiveInsert() {
    if (route_.midi == nullptr)
        return;

    for (auto channel = 0; channel < 16; ++channel)
        for (auto note = 0; note < 128; ++note)
            if (held_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(note)])
                route_.midi->sendAfterQueued(juce::MidiMessage::noteOff(channel + 1, note));
}

void LiveInsert::send(const BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
                      const juce::MidiBuffer& midi) {
    const auto channels = audio.getNumChannels();
    if (channels > 0 && !route_.sendChannels.empty()) {
        auto missing = false;
        for (std::size_t side = 0; side < route_.sendChannels.size(); ++side) {
            const auto* source = audio.getChannelPointer(std::min(side, channels - 1));
            missing |= !outputs_.add(route_.sendChannels[side], source, block.numSamples);
        }
        if (missing)
            missingSend_.fetch_add(1, std::memory_order_relaxed);
    }

    if (route_.midi == nullptr)
        return;

    for (const auto event : midi) {
        track(event);
        route_.midi->send(outputs_.segmentStart() + event.samplePosition, event.data,
                          event.numBytes);
    }
}

void LiveInsert::receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                         juce::MidiBuffer& /*midi*/) {
    if (audio.getNumChannels() > 0)
        return_.render(block, audio);
}

void LiveInsert::releaseNotes(const BlockInfo& /*block*/) {
    if (route_.midi == nullptr)
        return;

    for (auto channel = 0; channel < 16; ++channel) {
        auto& notes = held_[static_cast<std::size_t>(channel)];
        if (notes.none())
            continue;

        for (auto note = 0; note < 128; ++note) {
            if (!notes[static_cast<std::size_t>(note)])
                continue;

            const std::uint8_t off[] = {static_cast<std::uint8_t>(0x80 | channel),
                                        static_cast<std::uint8_t>(note), 0};
            route_.midi->send(outputs_.segmentStart(), off, 3);
        }
        notes.reset();
    }
}

void LiveInsert::track(const juce::MidiMessageMetadata& event) {
    if (event.numBytes < 3)
        return;

    const auto status = event.data[0] & 0xf0;
    const auto channel = static_cast<std::size_t>(event.data[0] & 0x0f);
    const auto note = static_cast<std::size_t>(event.data[1] & 0x7f);

    if (status == 0x90 && event.data[2] > 0)
        held_[channel].set(note);
    else if (status == 0x80 || status == 0x90)
        held_[channel].reset(note);
}

}  // namespace magda::engine
