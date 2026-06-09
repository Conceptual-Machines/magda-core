#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "../../core/ClipInfo.hpp"
#include "../../core/TypeIds.hpp"

namespace magda {

struct RecordingNoteEvent {
    int trackId = 0;
    int noteNumber = 0;
    int velocity = 0;
    bool isNoteOn = false;
    double transportSeconds = 0.0;
};

class RecordingNoteQueue {
  public:
    static constexpr int kQueueSize = 512;

    RecordingNoteQueue() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

    bool push(const RecordingNoteEvent& event) {
        int writeIdx = writeIndex_.load(std::memory_order_relaxed);
        int readIdx = readIndex_.load(std::memory_order_acquire);

        int nextWrite = (writeIdx + 1) & (kQueueSize - 1);
        if (nextWrite == readIdx)
            return false;

        buffer_[writeIdx] = event;
        writeIndex_.store(nextWrite, std::memory_order_release);
        return true;
    }

    bool pop(RecordingNoteEvent& event) {
        int writeIdx = writeIndex_.load(std::memory_order_acquire);
        int readIdx = readIndex_.load(std::memory_order_relaxed);

        if (readIdx == writeIdx)
            return false;

        event = buffer_[readIdx];
        readIndex_.store((readIdx + 1) & (kQueueSize - 1), std::memory_order_release);
        return true;
    }

    void clear() {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

  private:
    std::array<RecordingNoteEvent, kQueueSize> buffer_;
    std::atomic<int> writeIndex_{0};
    std::atomic<int> readIndex_{0};
};

struct AudioPeakSample {
    float peakL = 0.0f;  // Left channel peak (0.0 - 1.0+)
    float peakR = 0.0f;  // Right channel peak (0.0 - 1.0+)
};

/** Which recording context a preview belongs to. */
enum class RecordingTargetKind {
    Arrangement,  // Recording onto the arrangement timeline at startBeat.
    SessionSlot   // Recording into a session clip slot (trackId, sceneIndex).
};

/**
 * @brief Transient preview state for one active recording pass.
 *
 * This is the durable "active recording pass" model: a single growing,
 * beat-domain description of what is being captured right now, independent of
 * the final clip. Lives entirely outside ClipManager — no MAGDA clip is created
 * until recording finishes — and is consumed/replaced cleanly on stop.
 *
 * Arrangement passes are painted as an overlay by TrackContentPanel (using
 * startBeat as the timeline placement). Session-slot passes carry the same data
 * keyed by (trackId, sceneIndex); future takes/overdub work builds on this model.
 */
struct RecordingPreview {
    TrackId trackId = INVALID_TRACK_ID;
    RecordingTargetKind target = RecordingTargetKind::Arrangement;
    int sceneIndex = -1;  // Valid when target == SessionSlot.

    double startBeat = 0.0;           // Absolute timeline beat when the pass started.
    double currentLengthBeats = 0.0;  // Grows as the playhead advances (beats).
    std::vector<MidiNote> notes;      // Beats, relative to startBeat.

    // Audio waveform preview (one peak sample per update tick, ~30fps).
    // Presentation data only — never drives clip placement/timing.
    std::vector<AudioPeakSample> audioPeaks;
    bool isAudioRecording = false;  // True if this track records audio (not MIDI).
};

}  // namespace magda
