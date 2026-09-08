#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "io/RecordStream.hpp"
#include "tap/RecordTap.hpp"

/**
 * @file TakeNotes.hpp
 * @brief What a pass of MIDI is made of, for everything that reads one (#2462, #2463).
 *
 * The sibling of TakePasses.hpp: that holds the loop rules a take keeps, this
 * holds the note rules. The finished take and the pass in flight drive one of
 * these each, so a clip and the overlay drawn while it was recorded cannot
 * disagree about what a message is, which note a release closes, or what a
 * second strike does -- which is what they did disagree about (#2527 review).
 */

namespace magda::engine {

/// What one channel message is, as far as the model has a field for it.
enum class MidiKind : std::uint8_t { noteOn, noteOff, controller, pitchBend, unsupported };

/**
 * @brief The kind of a channel message, from its status and second data byte.
 *
 * A note on at zero velocity is a note off: the wire rule, and most controllers
 * never send 0x80 at all.
 */
constexpr MidiKind kindOf(std::uint8_t status, std::uint8_t data2) {
    switch (status & 0xf0U) {
        case 0x80U:
            return MidiKind::noteOff;

        case 0x90U:
            return data2 == 0 ? MidiKind::noteOff : MidiKind::noteOn;

        case 0xb0U:
            return MidiKind::controller;

        case 0xe0U:
            return MidiKind::pitchBend;

        default:
            return MidiKind::unsupported;
    }
}

inline MidiKind kindOf(const RecordedMidiEvent& event) {
    return kindOf(event.status, event.data2);
}

/**
 * @brief Which pitches are down, and what happens when one is struck again.
 *
 * An entry names a channel and a pitch; what a note *is* stays with the caller,
 * keyed by that entry, because the take holds a pending sample position and the
 * preview holds a published slot.
 *
 * The rule this exists for: a pitch already down is replaced rather than joined.
 * Nothing can close two of one pitch, so the second of them would run to the end
 * of the pass with no release able to reach it.
 *
 * Allocation-free once constructed, so the live half can drive one on the audio
 * thread.
 */
class HeldNotes {
  public:
    static constexpr std::size_t kChannels = 16;
    static constexpr std::size_t kNotesPerChannel = 128;
    static constexpr std::size_t kEntries = kChannels * kNotesPerChannel;

    HeldNotes() {
        position_.fill(0);
        down_.reserve(kEntries);
    }

    static std::uint32_t entryFor(int channel, int note) {
        return static_cast<std::uint32_t>(
            ((static_cast<std::size_t>(channel) % kChannels) * kNotesPerChannel) +
            (static_cast<std::size_t>(note) & (kNotesPerChannel - 1)));
    }

    /// The pitch an entry names. The channel is deliberately not asked for:
    /// MidiNote has no field for one.
    static int noteOf(std::uint32_t entry) {
        return static_cast<int>(entry % kNotesPerChannel);
    }

    bool isDown(std::uint32_t entry) const {
        return down_.size() > position_[entry] && down_[position_[entry]] == entry;
    }

    /**
     * @brief Mark the pitch down.
     *
     * True when it took over one that was already down, which is the second
     * strike the caller has to replace rather than add to.
     */
    bool hold(std::uint32_t entry) {
        if (isDown(entry))
            return true;

        position_[entry] = down_.size();
        down_.push_back(entry);
        return false;
    }

    /// @brief Lift the pitch. True when it was down, which is the only case
    /// with a note to close.
    bool release(std::uint32_t entry) {
        if (!isDown(entry))
            return false;

        const auto at = position_[entry];
        down_[at] = down_.back();
        position_[down_[at]] = at;
        down_.pop_back();
        return true;
    }

    /// Every pitch still down, in no order. A pass end closes all of them.
    std::span<const std::uint32_t> down() const {
        return down_;
    }

    void clear() {
        down_.clear();
    }

  private:
    /// Where each entry sits in @ref down_, meaningful only while it is there.
    std::array<std::size_t, kEntries> position_{};

    std::vector<std::uint32_t> down_;
};

/**
 * @brief A pass's notes as the tap holds them, over the table above.
 *
 * The one road from a MIDI transition to a published note. A take drives one of
 * these and so does every case about one, so there is no second answer to what
 * a strike on a pitch already down does, or to how far a held note has run.
 */
class PreviewNotes {
  public:
    explicit PreviewNotes(RecordTap& tap) : tap_(tap) {}

    /// @brief A pass begins at @p startBeat. Nothing is down in a new one, and
    /// a note held across the boundary belongs to the pass it started in.
    void open(double startBeat) {
        held_.clear();
        tap_.open(startBeat);
    }

    /// @brief A pitch goes down at @p beat from the pass start.
    void strike(int channel, int note, int velocity, double beat) {
        const auto entry = HeldNotes::entryFor(channel, note);
        auto& slot = slots_[entry];

        if (held_.hold(entry))
            tap_.replaceNote(slot, note, velocity, beat);
        else
            slot = tap_.addNote(note, velocity, beat);
    }

    /// @brief The pitch comes up at @p beat. A release with nothing down does
    /// nothing, as it does in the finished take.
    void release(int channel, int note, double beat) {
        const auto entry = HeldNotes::entryFor(channel, note);
        if (held_.release(entry))
            tap_.noteReaches(slots_[entry], beat);
    }

    /// @brief The pass, and every note still down, reach @p lengthBeats.
    void reaches(double lengthBeats) {
        tap_.extend(lengthBeats);

        for (const auto entry : held_.down())
            tap_.noteReaches(slots_[entry], lengthBeats);
    }

  private:
    RecordTap& tap_;

    HeldNotes held_;

    /// The slot each pitch is drawn in, meaningful while it is down.
    std::array<std::size_t, HeldNotes::kEntries> slots_{};
};

}  // namespace magda::engine
