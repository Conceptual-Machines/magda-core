#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace magda::engine {

/**
 * @brief How far into their samples a port's note-ons fall (#2741).
 *
 * Beside the juce::MidiBuffer that carries them, which counts whole samples.
 * Keyed by sample, channel and note rather than by position in the buffer, so
 * a merge is a concatenation and a transpose re-keys. Only fractions above zero
 * are kept: a note-on with none here falls on its sample exactly.
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
        if (fraction > 0.0f && entries_.size() < entries_.capacity())
            entries_.push_back({sample, channel, note, fraction});
    }

    /// Everything in @p other, @p shift samples later.
    void addFrom(const NoteFractions& other, int shift = 0) {
        for (const auto& entry : other.entries_)
            add(entry.sample + shift, entry.channel, entry.note, entry.fraction);
    }

    /// The fraction of the note-on at @p sample, or zero.
    float at(int sample, int channel, int note) const {
        for (const auto& entry : entries_)
            if (entry.sample == sample && entry.channel == channel && entry.note == note)
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

}  // namespace magda::engine
