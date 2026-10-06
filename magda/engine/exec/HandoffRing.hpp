#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "exec/NoteFractions.hpp"
#include "exec/RenderContext.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file HandoffRing.hpp
 * @brief What a plan's handoffs hold between the side rendered ahead and the callback (#1898).
 */

namespace magda::engine {

/// What identifies the block an entry was rendered for, so a block predicted differently from
/// the one the callback renders is a miss rather than the wrong audio.
struct BlockStamp {
    int numSamples = 0;
    bool playing = false;
    bool continuous = false;
    bool started = false;
    double beatsStart = 0.0;
    double beatsEnd = 0.0;
    double secondsStart = 0.0;
    SamplePosition monotonicStart{};

    static BlockStamp of(const BlockInfo& block) {
        return {.numSamples = block.numSamples,
                .playing = block.playing,
                .continuous = block.continuous,
                .started = block.started,
                .beatsStart = block.beats.start,
                .beatsEnd = block.beats.end,
                .secondsStart = block.seconds.start,
                .monotonicStart = block.monotonicSamples.start};
    }

    bool operator==(const BlockStamp&) const = default;
};

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
    bool canWrite(std::uint64_t block, bool split) {
        if (entries_.empty())
            return false;
        forgetDiscardedWholeBlocks();
        const auto released = released_.load(std::memory_order_acquire);
        const auto held = entryFor(block).stamp.load(std::memory_order_relaxed);
        return block >= released && (held == 0 || held <= released) &&
               (!split || released >= wholeUntil_);
    }

    /// Whether the reader has released everything the writer published.
    bool drained() const {
        return released_.load(std::memory_order_acquire) >=
               written_.load(std::memory_order_acquire);
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

    /// Writer: @p block is complete, rendered as @p stamp says.
    void publish(std::uint64_t block, const BlockStamp& stamp);

    /// Writer: the callback renders @p block whole; nothing was rendered ahead for it.
    void publishWhole(std::uint64_t block);

    /** @brief What the writer left for one block. */
    enum class Published : std::uint8_t { missing, whole, split };

    /// Reader: what @p block's entry holds; split only when rendered as @p stamp says.
    Published published(std::uint64_t block, const BlockStamp& stamp) const;

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

    /**
     * @brief Reader, holding the callback claim: forget every block from @p block on.
     *
     * For a block the ahead side rendered for a moment that did not come; the callback renders
     * it itself and the writer starts again after it (@ref discards).
     */
    void discardFrom(std::uint64_t block);

    /// How many times the reader has discarded; a writer seeing it move starts again.
    std::uint64_t discards() const {
        return discards_.load(std::memory_order_acquire);
    }

    /**
     * @brief Who is rendering the ahead side's state: nobody, the writer, or the callback.
     *
     * The writer claims each block it renders; the callback claims to render a block whole,
     * which runs the ahead side's ops too. Neither waits: a claim that fails is a block the
     * claimant does not render.
     */
    bool claimForWriter() {
        auto expected = Owner::none;
        return owner_.compare_exchange_strong(expected, Owner::writer, std::memory_order_acquire);
    }
    bool claimForCallback() {
        auto expected = Owner::none;
        return owner_.compare_exchange_strong(expected, Owner::callback, std::memory_order_acquire);
    }
    void releaseClaim() {
        owner_.store(Owner::none, std::memory_order_release);
    }

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
        BlockStamp rendered;
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
    std::atomic<std::uint64_t> discards_{0};

    enum class Owner : std::uint8_t { none, writer, callback };
    std::atomic<Owner> owner_{Owner::none};

    /// Writer only: one past the last block published whole, and the discards it has seen.
    std::uint64_t wholeUntil_ = 0;
    std::uint64_t writerDiscards_ = 0;

    /// Writer: a discard takes its whole blocks with it, and with them their barrier.
    void forgetDiscardedWholeBlocks() {
        const auto discards = discards_.load(std::memory_order_acquire);
        if (discards == writerDiscards_)
            return;
        writerDiscards_ = discards;
        wholeUntil_ = std::min(wholeUntil_, written_.load(std::memory_order_relaxed));
    }
};

}  // namespace magda::engine
