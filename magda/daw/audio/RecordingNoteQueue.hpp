#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "../core/ClipInfo.hpp"
#include "../core/TypeIds.hpp"

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

/**
 * @brief Transient preview data for a track that is currently recording.
 * Lives entirely outside ClipManager — no MAGDA clip is created until recording finishes.
 * Painted as an overlay by TrackContentPanel.
 */
struct AudioPeakSample {
    float peakL = 0.0f;  // Left channel peak (0.0 - 1.0+)
    float peakR = 0.0f;  // Right channel peak (0.0 - 1.0+)
};

struct RecordingPreview {
    TrackId trackId = INVALID_TRACK_ID;
    double startTime = 0.0;      // Transport position when recording started (seconds)
    double currentLength = 0.0;  // Grows as playhead advances (seconds)
    std::vector<MidiNote> notes;

    // Audio waveform preview (one peak sample per update tick, ~30fps)
    std::vector<AudioPeakSample> audioPeaks;
    bool isAudioRecording = false;  // True if this track records audio (not MIDI)
};

}  // namespace magda
