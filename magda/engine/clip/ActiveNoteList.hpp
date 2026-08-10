#pragma once

#include <array>
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
    }

    void clear(int channel, int note) {
        at(channel, note).clipId = INVALID_CLIP_ID;
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
        for (const auto& entry : entries_)
            if (entry.clipId != INVALID_CLIP_ID)
                return true;
        return false;
    }

    /// Every sounding note, as `f(channel, note)`. Channels are 1 to 16.
    template <typename Fn> void forEach(Fn&& fn) const {
        for (auto channel = 1; channel <= kChannels; ++channel)
            for (auto note = 0; note < kNotes; ++note)
                if (active(channel, note))
                    fn(channel, note);
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

    std::array<Entry, static_cast<std::size_t>(kChannels* kNotes)> entries_{};
};

}  // namespace magda::engine
