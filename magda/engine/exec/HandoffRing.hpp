#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "exec/NoteFractions.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file HandoffRing.hpp
 * @brief What a plan's handoffs hold between the side rendered ahead and the callback (#1898).
 */

namespace magda::engine {

/**
 * @brief Some blocks of every handoff's signal, stamped with the block each was rendered for.
 *
 * One writer, the ahead side, and one reader, the callback; they may be different threads. A
 * block is published by its stamp, and the writer reuses an entry only once the reader has
 * released the block in it.
 */
class HandoffRing {
  public:
    /// Room for @p depth blocks of each handoff in @p plan. @p midiBytes is the reservation of
    /// each op's output port 0, by op. Off the audio thread, with neither side running.
    void prepare(const RenderPlan& plan, int depth, int numChannels, int maxBlockSize,
                 const std::vector<int>& midiBytes);

    void reset();

    int depth() const {
        return static_cast<int>(entries_.size());
    }

    /// Whether @p op is a handoff this ring holds.
    bool holds(std::size_t op) const {
        return op < handoffForOp_.size() && handoffForOp_[op] >= 0;
    }

    /// Writer: whether @p block may be written: not yet released, and its entry empty or holding
    /// a released block. Any block may come first, so a ring prepared mid-stream starts where it
    /// is driven. A split block also waits until the callback has rendered every whole block
    /// before it.
    bool canWrite(std::uint64_t block, bool split) const {
        if (entries_.empty())
            return false;
        const auto released = released_.load(std::memory_order_acquire);
        const auto held = entryFor(block).stamp.load(std::memory_order_relaxed);
        return block >= released && (held == 0 || held <= released) &&
               (!split || released >= wholeUntil_);
    }

    /// Reader: whether the writer has published @p block or a later one, so the ahead side's
    /// state has moved past it.
    bool reached(std::uint64_t block) const {
        return written_.load(std::memory_order_acquire) > block;
    }

    /// Writer: where @p op's signal for @p block goes. Only between canWrite and publish.
    juce::dsp::AudioBlock<float> audio(std::size_t op, std::uint64_t block, int numSamples);
    juce::MidiBuffer& midi(std::size_t op, std::uint64_t block);
    NoteFractions& fractions(std::size_t op, std::uint64_t block);
    void setPanic(std::size_t op, std::uint64_t block, bool panic);

    /// Writer: @p block is complete, @p numSamples long.
    void publish(std::uint64_t block, int numSamples);

    /// Writer: the callback renders @p block whole; nothing was rendered ahead for it.
    void publishWhole(std::uint64_t block);

    /** @brief What the writer left for one block. */
    enum class Published : std::uint8_t { missing, whole, split };

    /// Reader: what @p block's entry holds; split only at @p numSamples.
    Published published(std::uint64_t block, int numSamples) const;

    int handoffCount() const {
        return entries_.empty() ? 0 : static_cast<int>(entries_.front()->handoffs.size());
    }

    /// Reader: @p op's signal for a ready @p block.
    juce::dsp::AudioBlock<const float> audio(std::size_t op, std::uint64_t block,
                                             int numSamples) const;
    const juce::MidiBuffer& midi(std::size_t op, std::uint64_t block) const;
    const NoteFractions& fractions(std::size_t op, std::uint64_t block) const;
    bool panic(std::size_t op, std::uint64_t block) const;

    /// Reader: done with everything up to and including @p block.
    void release(std::uint64_t block);

  private:
    struct Handoff {
        juce::AudioBuffer<float> audio;
        juce::MidiBuffer midi;
        NoteFractions fractions;
        bool panic = false;
    };

    struct Entry {
        /// The block in here plus one, or zero; written last, with release.
        std::atomic<std::uint64_t> stamp{0};
        int numSamples = 0;
        bool whole = false;
        std::vector<Handoff> handoffs;
    };

    Entry& entryFor(std::uint64_t block) {
        return *entries_[static_cast<std::size_t>(block % entries_.size())];
    }
    const Entry& entryFor(std::uint64_t block) const {
        return *entries_[static_cast<std::size_t>(block % entries_.size())];
    }
    Handoff& at(std::size_t op, std::uint64_t block) {
        return entryFor(block).handoffs[static_cast<std::size_t>(handoffForOp_[op])];
    }
    const Handoff& at(std::size_t op, std::uint64_t block) const {
        return entryFor(block).handoffs[static_cast<std::size_t>(handoffForOp_[op])];
    }

    void noteWritten(std::uint64_t block);

    std::vector<int> handoffForOp_;
    std::vector<std::unique_ptr<Entry>> entries_;

    /// One past the last block the reader released.
    std::atomic<std::uint64_t> released_{0};

    /// One past the latest block the writer published.
    std::atomic<std::uint64_t> written_{0};

    /// Writer only: one past the last block published whole.
    std::uint64_t wholeUntil_ = 0;
};

}  // namespace magda::engine
