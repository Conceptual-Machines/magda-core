#include "io/LiveInsert.hpp"

#include <algorithm>

namespace magda::engine {

LiveInsert::LiveInsert(const LiveInputFeed& inputs, LiveOutputFeed& outputs, LiveInsertRoute route)
    : outputs_(outputs),
      route_(std::move(route)),
      return_(inputs, route_.returnChannels),
      lastSent_(route_.ccs.size(), -1) {}

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

void LiveInsert::sendControls(const BlockInfo& block, const DeviceParams& params) {
    if (route_.control == nullptr || route_.ccs.empty())
        return;

    // A ramp is read every this many samples; a CC has only 128 steps to show it.
    constexpr int kRampStride = 64;
    const auto status = static_cast<std::uint8_t>(0xb0 | (juce::jlimit(1, 16, route_.channel) - 1));

    for (int entry = 0; entry < params.size(); ++entry) {
        const int slot = params.slotAt(entry);
        const auto found = std::find_if(route_.ccs.begin(), route_.ccs.end(),
                                        [slot](const auto& cc) { return cc.first == slot; });
        if (found == route_.ccs.end())
            continue;

        const auto values = params.valuesAt(entry);
        if (values.empty())
            continue;

        auto& last = lastSent_[static_cast<std::size_t>(found - route_.ccs.begin())];
        const int step = values.isConstant() ? block.numSamples : kRampStride;
        for (int offset = 0; offset < block.numSamples; offset += step) {
            const int value = juce::jlimit(0, 127, juce::roundToInt(values.valueAt(offset)));
            if (value == last)
                continue;
            last = value;
            const std::uint8_t message[] = {status, static_cast<std::uint8_t>(found->second & 0x7f),
                                            static_cast<std::uint8_t>(value)};
            route_.control->send(outputs_.segmentStart() + offset, message, 3);
        }
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
