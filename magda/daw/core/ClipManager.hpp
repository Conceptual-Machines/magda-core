#pragma once

#include <memory>
#include <vector>

#include "ClipInfo.hpp"
#include "ClipOperations.hpp"
#include "ClipTypes.hpp"
#include "TrackTypes.hpp"

namespace magda {

/**
 * @brief Listener interface for clip changes
 */
class ClipManagerListener {
  public:
    virtual ~ClipManagerListener() = default;

    // Called when clips are added, removed, or reordered
    virtual void clipsChanged() = 0;

    // Called when a specific clip's properties change
    virtual void clipPropertyChanged(ClipId clipId) {
        juce::ignoreUnused(clipId);
    }

    // Called when clip selection changes
    virtual void clipSelectionChanged(ClipId clipId) {
        juce::ignoreUnused(clipId);
    }

    // Called when clip playback state changes (session view)
    virtual void clipPlaybackStateChanged(ClipId clipId) {
        juce::ignoreUnused(clipId);
    }

    // Called during clip drag for real-time preview updates
    virtual void clipDragPreview(ClipId clipId, double previewStartTime, double previewLength) {
        juce::ignoreUnused(clipId, previewStartTime, previewLength);
    }
};

/**
 * @brief Singleton manager for all clips in the project
 *
 * Provides CRUD operations for clips and notifies listeners of changes.
 */
class ClipManager {
  public:
    static ClipManager& getInstance();

    // Prevent copying
    ClipManager(const ClipManager&) = delete;
    ClipManager& operator=(const ClipManager&) = delete;

    /**
     * @brief Shutdown and clear all resources
     * Call during app shutdown to prevent static cleanup issues
     */
    void shutdown() {
        arrangementClips_.clear();  // Clear JUCE objects before JUCE cleanup
        sessionClips_.clear();
    }

    // ========================================================================
    // Clip Creation
    // ========================================================================

    /**
     * @brief Create an audio clip from a file
     * @param view Which view the clip belongs to (Arrangement or Session)
     * @param startTime Position on timeline - only used for Arrangement view
     */
    ClipId createAudioClip(TrackId trackId, double startTime, double length,
                           const juce::String& audioFilePath, ClipView view = ClipView::Arrangement,
                           double projectBPM = 120.0);

    /**
     * @brief Create an empty MIDI clip
     * @param view Which view the clip belongs to (Arrangement or Session)
     * @param startTime Position on timeline - only used for Arrangement view
     */
    ClipId createMidiClip(TrackId trackId, double startTime, double length,
                          ClipView view = ClipView::Arrangement);

    /**
     * @brief Delete a clip
     */
    void deleteClip(ClipId clipId);

    /**
     * @brief Restore a clip from full ClipInfo (used by undo system)
     */
    void restoreClip(const ClipInfo& clipInfo);

    /**
     * @brief Force a clips changed notification (used by undo system)
     */
    void forceNotifyClipsChanged();

    /**
     * @brief Force a clip property changed notification for a specific clip
     * Used by commands that directly modify clip data without going through ClipManager methods
     */
    void forceNotifyClipPropertyChanged(ClipId clipId);

    /**
     * @brief Duplicate a clip (places copy right after original)
     * @return The ID of the new clip
     */
    ClipId duplicateClip(ClipId clipId);

    /**
     * @brief Duplicate a clip at a specific position
     * @param clipId The clip to duplicate
     * @param startTime Where to place the duplicate
     * @param trackId Track for the duplicate (INVALID_TRACK_ID = same track)
     * @return The ID of the new clip
     */
    ClipId duplicateClipAt(ClipId clipId, double startTime, TrackId trackId = INVALID_TRACK_ID);

    // ========================================================================
    // Clip Manipulation
    // ========================================================================

    /**
     * @brief Move clip to a new start time
     * @param tempo BPM for MIDI note shifting (notes maintain absolute timeline position)
     */
    void moveClip(ClipId clipId, double newStartTime, double tempo = 120.0);

    /**
     * @brief Move clip to a different track
     */
    void moveClipToTrack(ClipId clipId, TrackId newTrackId);

    /**
     * @brief Resize clip (change length)
     * @param fromStart If true, resize from the start edge (affects startTime)
     * @param tempo BPM for MIDI note shifting (required when fromStart=true for MIDI clips)
     */
    void resizeClip(ClipId clipId, double newLength, bool fromStart = false, double tempo = 120.0);

    /**
     * @brief Split a clip at a specific time
     * @return The ID of the new clip (right half)
     */
    ClipId splitClip(ClipId clipId, double splitTime, double tempo = 120.0);

    /**
     * @brief Trim clip to a range (used for time selection based creation)
     */
    void trimClip(ClipId clipId, double newStartTime, double newLength);

    // ========================================================================
    // Clip Properties
    // ========================================================================

    void setClipName(ClipId clipId, const juce::String& name);
    void setClipColour(ClipId clipId, juce::Colour colour);
    void setClipLoopEnabled(ClipId clipId, bool enabled, double projectBPM = 120.0);
    void setClipMidiOffset(ClipId clipId, double offsetBeats);
    void setClipLaunchMode(ClipId clipId, LaunchMode mode);
    void setClipLaunchQuantize(ClipId clipId, LaunchQuantize quantize);

    // Warp
    /** @brief Enable or disable warp markers on an audio clip */
    void setClipWarpEnabled(ClipId clipId, bool enabled);

    // Audio-specific (TE-aligned model)
    /** @brief Set the offset (start position) in the audio file (source-time seconds) - TE:
     * Clip::offset */
    void setOffset(ClipId clipId, double offset);
    /** @brief Set the loop phase (offset relative to loopStart) in loop mode */
    void setLoopPhase(ClipId clipId, double phase);
    /** @brief Set the loop region start in the audio file (source-time seconds) - TE:
     * AudioClipBase::loopStart
     * @param bpm Current tempo — used to update loopStartBeats when autoTempo is enabled */
    void setLoopStart(ClipId clipId, double loopStart, double bpm = 120.0);
    /** @brief Set the loop region length (source-time seconds) - TE: AudioClipBase::loopLength
     * @param bpm Current tempo — used to update loopLengthBeats when autoTempo is enabled */
    void setLoopLength(ClipId clipId, double loopLength, double bpm = 120.0);
    /** @brief Set the playback speed ratio (1.0 = original, 2.0 = double speed) - TE:
     * Clip::speedRatio */
    void setSpeedRatio(ClipId clipId, double speedRatio);
    /** @brief Set the time-stretch algorithm mode for an audio clip */
    void setTimeStretchMode(ClipId clipId, int mode);

    // Pitch
    void setAutoPitch(ClipId clipId, bool enabled);
    void setAutoPitchMode(ClipId clipId, int mode);
    void setPitchChange(ClipId clipId, float semitones);
    void setTranspose(ClipId clipId, int semitones);

    // Beat Detection
    void setAutoDetectBeats(ClipId clipId, bool enabled);
    void setBeatSensitivity(ClipId clipId, float sensitivity);

    // Playback
    void setIsReversed(ClipId clipId, bool reversed);

    // Per-Clip Mix
    void setClipGainDB(ClipId clipId, float dB);
    void setClipPan(ClipId clipId, float pan);

    // Fades
    void setFadeIn(ClipId clipId, double seconds);
    void setFadeOut(ClipId clipId, double seconds);
    void setFadeInType(ClipId clipId, int type);
    void setFadeOutType(ClipId clipId, int type);
    void setFadeInBehaviour(ClipId clipId, int behaviour);
    void setFadeOutBehaviour(ClipId clipId, int behaviour);
    void setAutoCrossfade(ClipId clipId, bool enabled);

    // Channels
    void setLeftChannelActive(ClipId clipId, bool active);
    void setRightChannelActive(ClipId clipId, bool active);

    // ========================================================================
    // Content-Level Operations (Editor Operations)
    // ========================================================================
    //
    // These methods wrap ClipOperations and provide automatic notification.
    // Use these for:
    // - Command pattern (undo/redo)
    // - External callers
    // - Non-interactive operations
    //
    // For interactive operations (drag), components may access clips directly
    // via getClip() and use ClipOperations for performance, then call
    // forceNotifyClipPropertyChanged() once on mouseUp.
    //
    // ========================================================================

    /**
     * @brief Trim/extend audio from left edge
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no constraint)
     */
    void trimAudioLeft(ClipId clipId, double trimAmount, double fileDuration = 0.0);

    /**
     * @brief Trim/extend audio from right edge
     * @param trimAmount Amount to trim in timeline seconds (positive=trim, negative=extend)
     * @param fileDuration Total file duration for constraint checking (0 = no constraint)
     */
    void trimAudioRight(ClipId clipId, double trimAmount, double fileDuration = 0.0);

    /**
     * @brief Stretch audio from left edge (editor operation)
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalSpeedRatio Original speed ratio at drag start
     */
    void stretchAudioLeft(ClipId clipId, double newLength, double oldLength,
                          double originalSpeedRatio);

    /**
     * @brief Stretch audio from right edge (editor operation)
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalSpeedRatio Original speed ratio at drag start
     */
    void stretchAudioRight(ClipId clipId, double newLength, double oldLength,
                           double originalSpeedRatio);

    // MIDI-specific
    void addMidiNote(ClipId clipId, const MidiNote& note);
    void removeMidiNote(ClipId clipId, int noteIndex);
    void clearMidiNotes(ClipId clipId);

    // ========================================================================
    // Access
    // ========================================================================

    /**
     * @brief Get all arrangement clips (timeline-based)
     */
    const std::vector<ClipInfo>& getArrangementClips() const {
        return arrangementClips_;
    }

    /**
     * @brief Get all session clips (scene-based)
     */
    const std::vector<ClipInfo>& getSessionClips() const {
        return sessionClips_;
    }

    /**
     * @brief Get all clips (both arrangement and session)
     * @deprecated Use getArrangementClips() or getSessionClips() instead
     */
    std::vector<ClipInfo> getClips() const;

    ClipInfo* getClip(ClipId clipId);
    const ClipInfo* getClip(ClipId clipId) const;

    /**
     * @brief Get all clips on a specific track
     */
    std::vector<ClipId> getClipsOnTrack(TrackId trackId) const;

    /**
     * @brief Get clip at a specific position on a track
     * @return INVALID_CLIP_ID if no clip at position
     */
    ClipId getClipAtPosition(TrackId trackId, double time) const;

    /**
     * @brief Get clips that overlap with a time range on a track
     */
    std::vector<ClipId> getClipsInRange(TrackId trackId, double startTime, double endTime) const;

    // ========================================================================
    // Selection
    // ========================================================================

    void setSelectedClip(ClipId clipId);
    ClipId getSelectedClip() const {
        return selectedClipId_;
    }
    void clearClipSelection();

    // ========================================================================
    // Clipboard Operations
    // ========================================================================

    /**
     * @brief Copy selected clips to clipboard
     * @param clipIds The clips to copy
     */
    void copyToClipboard(const std::unordered_set<ClipId>& clipIds);

    /**
     * @brief Paste clips from clipboard
     * @param pasteTime Timeline position to paste at
     * @param targetTrackId Track to paste on (INVALID_TRACK_ID = use original tracks)
     * @return IDs of the newly created clips
     */
    std::vector<ClipId> pasteFromClipboard(double pasteTime,
                                           TrackId targetTrackId = INVALID_TRACK_ID);

    /**
     * @brief Cut selected clips to clipboard (copy + delete)
     * @param clipIds The clips to cut
     */
    void cutToClipboard(const std::unordered_set<ClipId>& clipIds);

    /**
     * @brief Check if clipboard has clips
     */
    bool hasClipsInClipboard() const;

    /**
     * @brief Clear clipboard
     */
    void clearClipboard();

    // ========================================================================
    // Session View (Clip Launcher)
    // ========================================================================

    /**
     * @brief Get clip in a specific slot (track + scene)
     */
    ClipId getClipInSlot(TrackId trackId, int sceneIndex) const;

    /**
     * @brief Set scene index for a clip (assigns to session slot)
     */
    void setClipSceneIndex(ClipId clipId, int sceneIndex);

    /**
     * @brief Trigger/stop clip playback (session mode)
     */
    void triggerClip(ClipId clipId);
    void stopClip(ClipId clipId);
    void stopAllClips();

    /**
     * @brief Set the actual playing state of a session clip
     *
     * Called by SessionClipScheduler when a clip actually starts or stops producing audio.
     * This updates isPlaying/isQueued and notifies listeners.
     */
    void setClipPlayingState(ClipId clipId, bool playing);

    // ========================================================================
    // Listener Management
    // ========================================================================

    void addListener(ClipManagerListener* listener);
    void removeListener(ClipManagerListener* listener);

    /**
     * @brief Broadcast drag preview event (called during clip drag for real-time updates)
     */
    void notifyClipDragPreview(ClipId clipId, double previewStartTime, double previewLength);

    // ========================================================================
    // Project Management
    // ========================================================================

    void clearAllClips();

    /**
     * @brief Create random test clips for development
     */
    void createTestClips();

  private:
    ClipManager() = default;
    ~ClipManager() = default;

    // Separate storage for arrangement and session clips
    std::vector<ClipInfo> arrangementClips_;
    std::vector<ClipInfo> sessionClips_;

    // Clipboard storage
    std::vector<ClipInfo> clipboard_;
    double clipboardReferenceTime_ = 0.0;  // For maintaining relative positions

    std::vector<ClipManagerListener*> listeners_;
    int nextClipId_ = 1;
    ClipId selectedClipId_ = INVALID_CLIP_ID;

    // Notification helpers
    void notifyClipsChanged();
    void notifyClipPropertyChanged(ClipId clipId);
    void notifyClipSelectionChanged(ClipId clipId);
    void notifyClipPlaybackStateChanged(ClipId clipId);

    // Clamp audio clip properties (offset, loopStart, loopLength) to file bounds
    void sanitizeAudioClip(ClipInfo& clip);

    // Helper to generate unique clip name
    juce::String generateClipName(ClipType type) const;
};

}  // namespace magda
