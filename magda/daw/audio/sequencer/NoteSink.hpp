#pragma once

#include <array>

namespace magda::daw::audio::sequencer {

/**
 * @brief One note the sequencer decided to play or release.
 *
 * A plain record rather than a MIDI message: the core names no MIDI container,
 * so the same engine drives a live device, the offline clip export, and a test
 * (#2313). @p timeInBlock is seconds from the start of the block that produced
 * it, which is the timestamp domain MAGDA's devices hand their hosts.
 */
struct NoteEvent {
    double timeInBlock = 0.0;
    int noteNumber = 0;
    int velocity = 0;  ///< 1-127 on a note-on; unused on a note-off
    bool isNoteOn = false;
};

/// Where a sequencer writes the notes it produces.
class NoteSink {
  public:
    virtual ~NoteSink() = default;
    virtual void addNoteEvent(const NoteEvent& event) = 0;
};

/**
 * @brief A sink that keeps what it is given, up to @p Capacity events.
 *
 * For callers with no buffer of their own - offline walks and tests. Events
 * past the capacity are dropped and counted, so an overflow is visible rather
 * than silent.
 */
template <int Capacity> class NoteEventList : public NoteSink {
  public:
    void addNoteEvent(const NoteEvent& event) override {
        if (count_ >= Capacity) {
            ++dropped_;
            return;
        }
        events_[static_cast<size_t>(count_++)] = event;
    }

    void clear() {
        count_ = 0;
        dropped_ = 0;
    }

    int size() const {
        return count_;
    }
    int dropped() const {
        return dropped_;
    }
    const NoteEvent& operator[](int index) const {
        return events_[static_cast<size_t>(index)];
    }
    const NoteEvent* begin() const {
        return events_.data();
    }
    const NoteEvent* end() const {
        return events_.data() + count_;
    }

  private:
    std::array<NoteEvent, Capacity> events_{};
    int count_ = 0;
    int dropped_ = 0;
};

}  // namespace magda::daw::audio::sequencer
