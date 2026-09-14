#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>

#include "tap/MidiTap.hpp"

/**
 * @file NoteOnTap.hpp
 * @brief Which notes started at a point in the MIDI, for a pad's trigger light (#2669).
 */

namespace magda::engine {

/**
 * @brief The notes a MIDI op passed a note-on for since the last take.
 *
 * One writer (the audio thread) and one reader, like a LevelTap: a take is
 * destructive, so a hit between two reads is held rather than missed.
 */
class NoteOnTap final : public MidiTap {
  public:
    using Notes = std::bitset<128>;

    void write(const juce::MidiBuffer& midi, const BlockInfo& /*block*/) override {
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (!message.isNoteOn())
                continue;

            const auto note = static_cast<unsigned>(message.getNoteNumber());
            words_[note / 64].fetch_or(std::uint64_t{1} << (note % 64), std::memory_order_acq_rel);
        }
    }

    /** @brief Take the notes started since the last take. Off the audio thread. */
    Notes take() {
        Notes notes;
        for (std::size_t word = 0; word < words_.size(); ++word) {
            const auto bits = words_[word].exchange(0, std::memory_order_acq_rel);
            for (std::size_t bit = 0; bit < 64; ++bit)
                if ((bits >> bit) & 1u)
                    notes.set(word * 64 + bit);
        }
        return notes;
    }

  private:
    std::array<std::atomic<std::uint64_t>, 2> words_{};
};

}  // namespace magda::engine
