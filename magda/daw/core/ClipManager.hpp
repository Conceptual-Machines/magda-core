#pragma once

#include <memory>
#include <vector>

#include "ClipInfo.hpp"
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
        clips_.clear();  // Clear JUCE objects before JUCE cleanup
    }

    // ========================================================================
    // Clip Creation
    // ========================================================================

    /**
     * @brief Create an audio clip from a file
     */
    ClipId createAudioClip(TrackId trackId, double startTime, double length,
                           const juce::String& audioFilePath);

    /**
     * @brief Create an empty MIDI clip
     */
    ClipId createMidiClip(TrackId trackId, double startTime, double length);

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
    ClipId splitClip(ClipId clipId, double splitTime);

    /**
     * @brief Trim clip to a range (used for time selection based creation)
     */
    void trimClip(ClipId clipId, double newStartTime, double newLength);

    // ========================================================================
    // Clip Properties
    // ========================================================================

    void setClipName(ClipId clipId, const juce::String& name);
    void setClipColour(ClipId clipId, juce::Colour colour);
    void setClipLoopEnabled(ClipId clipId, bool enabled);
    void setClipLoopLength(ClipId clipId, double lengthBeats);

    // Audio-specific
    void setAudioSourcePosition(ClipId clipId, int sourceIndex, double position);
    void setAudioSourceLength(ClipId clipId, int sourceIndex, double length);

    // MIDI-specific
    void addMidiNote(ClipId clipId, const MidiNote& note);
    void removeMidiNote(ClipId clipId, int noteIndex);
    void clearMidiNotes(ClipId clipId);

    // ========================================================================
    // Access
    // ========================================================================

    const std::vector<ClipInfo>& getClips() const {
        return clips_;
    }

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

    std::vector<ClipInfo> clips_;
    std::vector<ClipManagerListener*> listeners_;
    int nextClipId_ = 1;
    ClipId selectedClipId_ = INVALID_CLIP_ID;

    // Notification helpers
    void notifyClipsChanged();
    void notifyClipPropertyChanged(ClipId clipId);
    void notifyClipSelectionChanged(ClipId clipId);
    void notifyClipPlaybackStateChanged(ClipId clipId);

    // Helper to generate unique clip name
    juce::String generateClipName(ClipType type) const;
};

}  // namespace magda
