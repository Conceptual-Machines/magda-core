#pragma once

#include <string>
#include <unordered_map>

#include "../audio/midi/RecordingNoteQueue.hpp"
#include "../core/ClipTypes.hpp"
#include "../core/TempoMap.hpp"
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
        Returns -1.0 if no session clips are playing. Tracks the most recently launched clip. */
    virtual double getSessionPlayheadPosition() const = 0;

    /** Returns the clip ID the session playhead currently tracks, or INVALID_CLIP_ID. */
    virtual ClipId getSessionPlayheadClipId() const = 0;

    /** Returns per-clip playhead positions for all active session clips. */
    virtual std::unordered_map<ClipId, double> getActiveClipPlayheadPositions() const = 0;

    /** Returns the play state of a session clip (Stopped/Queued/Playing). */
    virtual SessionClipPlayState getSessionClipPlayState(ClipId clipId) const = 0;

    /** Schedule a quantized stop for the active clip on a track (empty slot in scene). */
    virtual void stopSessionTrack(TrackId trackId) = 0;

    /** True while a quantized stop on this track is in flight (between
        `stopSessionTrack` and the LaunchHandle reporting Stopped). The UI
        uses this to blink the empty-slot stop affordance. */
    virtual bool isSessionTrackStopPending(TrackId trackId) const = 0;

    /** Latest transport position (seconds) sampled by the audio thread.
        Returns -1.0 if the audio thread has not run yet. Used by
        beat-aligned visuals (beat indicator, etc.) so they stay phase-locked
        with audio rather than drifting at the message-thread sampling rate. */
    virtual double getAudioThreadTransportSeconds() const = 0;

    /** Stop all session clips, clear active state, revert to arrangement. */
    virtual void deactivateAllSessionClips() = 0;

    /** Mark an empty session slot as the target for recording. */
    virtual void armSessionSlotRecording(TrackId /*trackId*/, int /*sceneIndex*/) {}

    /** Begin any armed session slot recordings. */
    virtual void beginArmedSessionSlotRecordings() {}

    /** True if the given empty session slot is armed as a recording target. */
    virtual bool isSessionSlotRecordArmed(TrackId /*trackId*/, int /*sceneIndex*/) const {
        return false;
    }

    /** True while the given session slot is actively recording. */
    virtual bool isSessionSlotRecording(TrackId /*trackId*/, int /*sceneIndex*/) const {
        return false;
    }

    // ===== Tempo =====
    virtual void setTempo(double bpm) = 0;
    virtual double getTempo() const = 0;
    virtual void setTimeSignature(int numerator, int denominator) = 0;

    /** Position-aware beats<->seconds facade backed by the engine's tempo
        sequence (the single source of truth). The UI injects this into
        TimelineController so every conversion walks the tempo curve. The
        returned pointer is owned by the engine and valid for its lifetime.
        Defaults to null for engines/mocks that don't provide one. */
    virtual const TempoMap* tempoMap() const {
        return nullptr;
    }

    // ===== Loop =====
    virtual void setLooping(bool enabled) = 0;
    virtual void setLoopRegion(double startSeconds, double endSeconds) = 0;
    virtual bool isLooping() const = 0;

    // ===== Metronome =====
    virtual void setMetronomeEnabled(bool enabled) = 0;
    virtual bool isMetronomeEnabled() const = 0;

    // Count-in / pre-roll (0=none, 1=1bar, 2=2bars, 3=2beats, 4=1beat)
    virtual void setCountInMode(int mode) = 0;
    virtual int getCountInMode() const = 0;

    // ===== Trigger State (for transport-synced devices) =====
    virtual void updateTriggerState() = 0;

    // ===== Session State Events (audio thread → message thread) =====
    virtual void processSessionStateEvents() = 0;

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
