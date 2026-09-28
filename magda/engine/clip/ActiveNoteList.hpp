#pragma once

#include <array>
#include <bit>
#include <cstdint>

#include "core/ClipTypes.hpp"

/**
 * @file ActiveNoteList.hpp
 * @brief What is sounding, who started it, and what is owed.
 *
 * The invariant the MIDI slice is judged on: a source never emits a note-off
 * for a note it did not start, and never fails to emit one for a note it did.
 * This is what makes that structural rather than careful. It outlives every
 * clip, block and plan that passes through the source, because a note does too.
 *
 * Owned per track rather than per clip, because a MIDI port is per track and two
 * clips sounding the same pitch on the same channel are indistinguishable to
 * whatever receives them. Which clip started a note is carried so that a clip
 * ending, or vanishing from the snapshot, can end its own notes without ending
 * its neighbour's.
 *
 * The note-on's timeline beat is carried for one reason: groove moves both edges
 * of a note independently, at different beats, so a grooved note-off can land
 * before its own note-on. Knowing where the on actually went is what lets the
 * off be clamped after it.
 */

namespace magda::engine {

class ActiveNoteList {
  public:
    static constexpr int kChannels = 16;
    static constexpr int kNotes = 128;

    void start(int channel, int note, ClipId clipId, double timelineBeat) {
        auto& entry = at(channel, note);
        entry.clipId = clipId;
        entry.startBeat = timelineBeat;
        mark(channel, note, clipId != INVALID_CLIP_ID);
    }

    void clear(int channel, int note) {
        at(channel, note).clipId = INVALID_CLIP_ID;
        mark(channel, note, false);
    }

    bool active(int channel, int note) const {
        return at(channel, note).clipId != INVALID_CLIP_ID;
    }

    ClipId owner(int channel, int note) const {
        return at(channel, note).clipId;
    }

    /// Where the note-on actually sounded, which is not where the model put it
    /// when a groove moved it.
    double startBeat(int channel, int note) const {
        return at(channel, note).startBeat;
    }

    bool any() const {
        for (const auto word : sounding_)
            if (word != 0)
                return true;
        return false;
    }

    /// Every sounding note, as `f(channel, note)`, by channel then pitch. Channels are 1 to 16.
    /// Walks the sounding bits rather than every entry: a stopped slot asks every block.
    template <typename Fn> void forEach(Fn&& fn) const {
        for (auto word = 0; word < kWords; ++word)
            for (auto bits = sounding_[static_cast<std::size_t>(word)]; bits != 0;
                 bits &= bits - 1) {
                const auto bit = word * 64 + std::countr_zero(bits);
                fn(bit / kNotes + 1, bit % kNotes);
            }
    }

  private:
    struct Entry {
        ClipId clipId = INVALID_CLIP_ID;
        double startBeat = 0.0;
    };

    static std::size_t index(int channel, int note) {
        return static_cast<std::size_t>((channel - 1) * kNotes + note);
    }

    Entry& at(int channel, int note) {
        return entries_[index(channel, note)];
    }
    const Entry& at(int channel, int note) const {
        return entries_[index(channel, note)];
    }

    void mark(int channel, int note, bool sounding) {
        const auto bit = index(channel, note);
        const auto mask = std::uint64_t{1} << (bit % 64);
        auto& word = sounding_[bit / 64];
        word = sounding ? (word | mask) : (word & ~mask);
    }

    static constexpr int kWords = kChannels * kNotes / 64;

    std::array<Entry, static_cast<std::size_t>(kChannels* kNotes)> entries_{};
    /// One bit per entry, set while its note sounds.
    std::array<std::uint64_t, static_cast<std::size_t>(kWords)> sounding_{};
};

}  // namespace magda::engine
