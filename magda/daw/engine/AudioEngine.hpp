#pragma once

#include <string>
#include <unordered_map>

#include "../audio/RecordingNoteQueue.hpp"
#include "../ui/state/TransportStateListener.hpp"

namespace juce {
class AudioDeviceManager;
}

namespace magda {

/**
 * @brief Abstract audio engine interface
 *
 * This provides a clean abstraction over the actual audio engine implementation.
 * Concrete implementations (e.g., TracktionEngineWrapper) inherit from this.
 *
 * Also inherits from AudioEngineListener so the TimelineController can notify
 * the audio engine of state changes via the observer pattern.
 */
class AudioEngine : public AudioEngineListener {
  public:
    ~AudioEngine() override = default;

    // ===== Lifecycle =====
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // ===== Transport =====
    virtual void play() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void record() = 0;
    virtual void locate(double positionSeconds) = 0;
    virtual double getCurrentPosition() const = 0;
    virtual bool isPlaying() const = 0;
    virtual bool isRecording() const = 0;

    /** Returns the looped playhead position within the active session clip (seconds).
        Returns -1.0 if no session clips are playing. */
    virtual double getSessionPlayheadPosition() const = 0;

    // ===== Tempo =====
    virtual void setTempo(double bpm) = 0;
    virtual double getTempo() const = 0;
    virtual void setTimeSignature(int numerator, int denominator) = 0;

    // ===== Loop =====
    virtual void setLooping(bool enabled) = 0;
    virtual void setLoopRegion(double startSeconds, double endSeconds) = 0;
    virtual bool isLooping() const = 0;

    // ===== Metronome =====
    virtual void setMetronomeEnabled(bool enabled) = 0;
    virtual bool isMetronomeEnabled() const = 0;

    // ===== Trigger State (for transport-synced devices) =====
    virtual void updateTriggerState() = 0;

    // ===== Device Management =====
    virtual juce::AudioDeviceManager* getDeviceManager() = 0;

    // ===== Audio Management =====
    virtual class AudioBridge* getAudioBridge() = 0;
    virtual const class AudioBridge* getAudioBridge() const = 0;

    // ===== MIDI Management =====
    virtual class MidiBridge* getMidiBridge() = 0;
    virtual const class MidiBridge* getMidiBridge() const = 0;

    // ===== MIDI Preview =====
    /**
     * @brief Preview a MIDI note on a track (for keyboard audition)
     * @param track_id Track ID to send note to
     * @param noteNumber MIDI note number (0-127)
     * @param velocity Velocity (0-127), 0 for note-off
     * @param isNoteOn True for note-on, false for note-off
     */
    virtual void previewNoteOnTrack(const std::string& track_id, int noteNumber, int velocity,
                                    bool isNoteOn) = 0;

    // ===== Recording Preview =====
    /**
     * @brief Get active recording previews for real-time MIDI note display
     * Returns transient preview data that exists only during recording.
     * No ClipManager clips are involved — this is paint-only overlay data.
     */
    virtual const std::unordered_map<TrackId, RecordingPreview>& getRecordingPreviews() const {
        static const std::unordered_map<TrackId, RecordingPreview> empty;
        return empty;
    }
};

}  // namespace magda
