#include "exec/HandoffRing.hpp"

#include <algorithm>

#include "exec/EngineDevice.hpp"

namespace magda::engine {

void HandoffRing::prepare(const RenderPlan& plan, int depth, int numChannels, int maxBlockSize,
                          const std::vector<int>& midiBytes) {
    reset();

    std::vector<std::size_t> handoffOps;
    handoffForOp_.assign(plan.ops.size(), -1);
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        if (plan.ops[i].kind != OpKind::Handoff)
            continue;
        handoffForOp_[i] = static_cast<int>(handoffOps.size());
        handoffOps.push_back(i);
    }

    entries_.resize(static_cast<std::size_t>(std::max(depth, 1)));
    for (auto& entry : entries_) {
        entry = std::make_unique<Entry>();
        entry->handoffs.resize(handoffOps.size());
        for (std::size_t h = 0; h < handoffOps.size(); ++h) {
            const auto op = handoffOps[h];
            auto& handoff = entry->handoffs[h];
            if (plan.ops[op].outputs.front().kind == SignalKind::Audio) {
                handoff.audio.setSize(numChannels, maxBlockSize, false, true, false);
                continue;
            }
            const auto bytes = op < midiBytes.size() ? midiBytes[op] : 0;
            handoff.midi.ensureSize(static_cast<std::size_t>(bytes));
            handoff.fractions.reserve(static_cast<std::size_t>(bytes / kMidiShortMessageBytes + 1));
        }
    }
}

void HandoffRing::reset() {
    handoffForOp_.clear();
    entries_.clear();
    released_.store(0, std::memory_order_relaxed);
    written_.store(0, std::memory_order_relaxed);
    wholeUntil_ = 0;
}

juce::dsp::AudioBlock<float> HandoffRing::audio(std::size_t op, std::uint64_t block,
                                                int numSamples) {
    return juce::dsp::AudioBlock<float>(at(op, block).audio)
        .getSubBlock(0, static_cast<std::size_t>(numSamples));
}

juce::MidiBuffer& HandoffRing::midi(std::size_t op, std::uint64_t block) {
    return at(op, block).midi;
}

NoteFractions& HandoffRing::fractions(std::size_t op, std::uint64_t block) {
    return at(op, block).fractions;
}

void HandoffRing::setPanic(std::size_t op, std::uint64_t block, bool panic) {
    at(op, block).panic = panic;
}

void HandoffRing::publish(std::uint64_t block, int numSamples) {
    auto& entry = entryFor(block);
    entry.numSamples = numSamples;
    entry.whole = false;
    // Reached before stamped: a reader between the two takes a miss, never a second render.
    noteWritten(block);
    entry.stamp.store(block + 1, std::memory_order_release);
}

void HandoffRing::publishWhole(std::uint64_t block) {
    auto& entry = entryFor(block);
    entry.numSamples = 0;
    entry.whole = true;
    wholeUntil_ = std::max(wholeUntil_, block + 1);
    noteWritten(block);
    entry.stamp.store(block + 1, std::memory_order_release);
}

HandoffRing::Published HandoffRing::published(std::uint64_t block, int numSamples) const {
    if (entries_.empty())
        return Published::missing;
    const auto& entry = entryFor(block);
    if (entry.stamp.load(std::memory_order_acquire) != block + 1)
        return Published::missing;
    if (entry.whole)
        return Published::whole;
    return entry.numSamples == numSamples ? Published::split : Published::missing;
}

juce::dsp::AudioBlock<const float> HandoffRing::audio(std::size_t op, std::uint64_t block,
                                                      int numSamples) const {
    const auto& buffer = at(op, block).audio;
    return juce::dsp::AudioBlock<const float>(buffer.getArrayOfReadPointers(),
                                              static_cast<std::size_t>(buffer.getNumChannels()),
                                              static_cast<std::size_t>(numSamples));
}

const juce::MidiBuffer& HandoffRing::midi(std::size_t op, std::uint64_t block) const {
    return at(op, block).midi;
}

const NoteFractions& HandoffRing::fractions(std::size_t op, std::uint64_t block) const {
    return at(op, block).fractions;
}

bool HandoffRing::panic(std::size_t op, std::uint64_t block) const {
    return at(op, block).panic;
}

void HandoffRing::noteWritten(std::uint64_t block) {
    if (block + 1 > written_.load(std::memory_order_relaxed))
        written_.store(block + 1, std::memory_order_release);
}

void HandoffRing::release(std::uint64_t block) {
    if (block + 1 > released_.load(std::memory_order_relaxed))
        released_.store(block + 1, std::memory_order_release);
}

}  // namespace magda::engine
