#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace magda::engine {

/**
 * @brief How far into their samples a port's note-ons fall (#2741).
 *
 * Beside the juce::MidiBuffer that carries them, which counts whole samples.
 * One entry per note-on, and at any one sample in the order the buffer holds
 * them, which every writer keeps by adding an entry as it adds the event. Keyed
 * by sample, channel and note rather than by position in the buffer, so a
 * merge is a concatenation and a transpose re-keys; the nth note-on of a pitch
 * at a sample is its nth entry.
 */
class NoteFractions {
  public:
    struct Entry {
        int sample = 0;
        int channel = 1;
        int note = 0;
        float fraction = 0.0f;
    };

    NoteFractions() = default;
    explicit NoteFractions(std::size_t capacity) {
        reserve(capacity);
    }

    /// Off the audio thread. Past it, add() drops rather than allocates, and the
    /// note falls on its sample.
    void reserve(std::size_t capacity) {
        entries_.reserve(capacity);
    }

    void clear() {
        entries_.clear();
    }

    void add(int sample, int channel, int note, float fraction) {
        if (entries_.size() < entries_.capacity())
            entries_.push_back({sample, channel, note, fraction});
    }

    /// An entry at no fraction for every note-on in @p events, for a writer
    /// whose events fall on their samples.
    void addWhole(const juce::MidiBuffer& events) {
        for (const auto metadata : events)
            if (const auto message = metadata.getMessage(); message.isNoteOn())
                add(metadata.samplePosition, message.getChannel(), message.getNoteNumber(), 0.0f);
    }

    /// Everything in @p other, @p shift samples later.
    void addFrom(const NoteFractions& other, int shift = 0) {
        for (const auto& entry : other.entries_)
            add(entry.sample + shift, entry.channel, entry.note, entry.fraction);
    }

    /// The fraction of the @p occurrence th note-on of this pitch at @p sample,
    /// counting from zero, or zero where there is none.
    float at(int sample, int channel, int note, int occurrence = 0) const {
        for (const auto& entry : entries_)
            if (entry.sample == sample && entry.channel == channel && entry.note == note &&
                occurrence-- == 0)
                return entry.fraction;
        return 0.0f;
    }

    std::span<const Entry> entries() const {
        return entries_;
    }

    bool empty() const {
        return entries_.empty();
    }

  private:
    std::vector<Entry> entries_;
};

/**
 * @brief Which occurrence of its pitch a note-on is, walking a buffer in order.
 *
 * What NoteFractions::at() asks for, counted within the current sample. A
 * counter per channel and note, emptied by moving to a new generation rather
 * than by clearing, so neither the count of events nor of pitches is bounded.
 */
class NoteOccurrences {
  public:
    /// Before a buffer is walked: its first sample starts afresh.
    void restart() {
        sample_ = -1;
    }

    int next(int sample, int channel, int note) {
        if (sample != sample_) {
            sample_ = sample;
            ++generation_;
        }

        const auto key = static_cast<std::size_t>(std::clamp(channel - 1, 0, 15) * 128 +
                                                  std::clamp(note, 0, 127));
        if (generation_of_[key] != generation_) {
            generation_of_[key] = generation_;
            count_[key] = 0;
        }
        return count_[key]++;
    }

  private:
    int sample_ = -1;
    std::uint64_t generation_ = 0;
    std::array<std::uint64_t, 16 * 128> generation_of_{};
    std::array<int, 16 * 128> count_{};
};

}  // namespace magda::engine
