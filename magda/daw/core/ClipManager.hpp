#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ClipFades.hpp"
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

    // Called when multiple clips' properties change in a batch (e.g. multi-selection drag).
    // Default falls back to per-clip notifications; override for batch optimisation.
    virtual void clipPropertiesChanged(const std::vector<ClipId>& clipIds) {
        for (auto id : clipIds)
            clipPropertyChanged(id);
    }

    // Called when clip selection changes
    virtual void clipSelectionChanged(ClipId clipId) {
        juce::ignoreUnused(clipId);
    }

    // Called when clip playback state changes (session view)
    virtual void clipPlaybackStateChanged(ClipId clipId) {
        juce::ignoreUnused(clipId);
    }

    // Called when a clip playback is requested (Play or Stop)
    virtual void clipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) {
        juce::ignoreUnused(clipId, request);
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
        clips_.clear();
        sessionSlotIndex_.clear();
    }

    // ========================================================================
    // Clip Creation
    // ========================================================================

    /**
     * @brief Create an audio clip from a file — beats-authoritative API.
     *
     * Source duration, offset, and loop fields remain source-domain seconds.
     * Timeline placement is stored in beats and seconds are derived only for
     * bridge/UI compatibility.
     */
    ClipId createAudioClipBeats(
        TrackId trackId, double startBeats, double lengthBeats, const juce::String& audioFilePath,
        ClipView view = ClipView::Arrangement, double projectBPM = 0.0,
        ClipOverlapPolicy overlapPolicy = ClipOverlapPolicy::PreserveExisting);

    /**
     * @brief Create an audio clip from timeline seconds.
     *
     * Thin shim around createAudioClipBeats for UI/engine boundaries whose
     * natural input is still seconds.
     */
    ClipId createAudioClip(TrackId trackId, double startTime, double length,
                           const juce::String& audioFilePath, ClipView view = ClipView::Arrangement,
                           double projectBPM = 0.0,
                           ClipOverlapPolicy overlapPolicy = ClipOverlapPolicy::PreserveExisting);

    /**
     * @brief Create an empty MIDI clip — beats-authoritative API.
     *
     * Beats are the canonical positioning unit for MIDI clips in MAGDA.
     * Use this whenever the caller has musical units (bars, beats, ticks)
     * — never compute beats by going through seconds first. Seconds are
     * derived from the project tempo at clip-creation time and stored as
     * a display cache only; they are NOT round-tripped back into beats.
     */
    ClipId createMidiClipBeats(
        TrackId trackId, double startBeats, double lengthBeats,
        ClipView view = ClipView::Arrangement,
        ClipOverlapPolicy overlapPolicy = ClipOverlapPolicy::PreserveExisting);

    /**
     * @brief Create an empty MIDI clip from seconds.
     *
     * Thin shim around createMidiClipBeats — only legitimate when the
     * caller's natural unit is seconds (recording from the audio thread,
     * timeline drops measured in pixels-as-time). Anything driven by
     * musical input should call createMidiClipBeats directly.
     *
     * @param view Which view the clip belongs to (Arrangement or Session)
     * @param startTime Position on timeline - only used for Arrangement view
     */
    ClipId createMidiClip(TrackId trackId, double startTime, double length,
                          ClipView view = ClipView::Arrangement,
                          ClipOverlapPolicy overlapPolicy = ClipOverlapPolicy::PreserveExisting);

    /**
     * @brief Switch the active MIDI loop-record take (#1465).
     *
     * Fronts take `takeIndex` — copies its notes/CC/pitchbend into the clip's
     * authoritative event vectors and re-syncs to the engine. No-op unless the
     * clip is MIDI with that take.
     */
    void setMidiClipCurrentTake(ClipId clipId, int takeIndex);

    /** Switch the active AUDIO take (undoable; exits any comp). */
    void setAudioClipCurrentTake(ClipId clipId, int takeIndex);

    /**
     * @brief Assign MIDI comp section [startBeat, endBeat) to a take (#1466).
     *
     * Seeds a full-length base section from the current take on first use, then
     * splits/merges so the range plays `takeIndex`, reassembles the active note
     * list, and re-syncs. No render (unlike audio). No-op for non-MIDI clips or
     * clips with fewer than two takes.
     */
    void setMidiCompSection(ClipId clipId, double startBeat, double endBeat, int takeIndex);

    /** Drop the MIDI comp and revert to the current take. */
    void clearMidiComp(ClipId clipId);

    /**
     * @brief Delete loop-record take `takeIndex` (audio or MIDI).
     *
     * Drops the take, remaps comp sections (sections on the deleted take are
     * removed, higher indices shift down), clamps the active take, re-fronts the
     * active content, and re-syncs. No-op for the last remaining take.
     */
    void deleteClipTake(ClipId clipId, int takeIndex);

    /**
     * @brief Delete a clip
     */
    void deleteClip(ClipId clipId);

    /**
     * @brief Restore a clip from full ClipInfo (used by undo system)
     */
    void restoreClip(const ClipInfo& clipInfo);

    /**
     * @brief Overwrite an existing clip's full state in place and re-sync.
     *
     * Used by the take/comp undo commands to restore a snapshot. Unlike
     * restoreClip (which re-adds a deleted clip), this replaces a live clip.
     */
    void replaceClipState(const ClipInfo& clipInfo);

    /**
     * @brief Push an undoable take/comp snapshot. `before` is the clip state
     * captured before the edit; the current (post-edit) state is the redo state.
     * Used by audio comp edits in CompService.
     */
    void pushClipTakeUndo(const juce::String& desc, const ClipInfo& before);

    /**
     * @brief Force a clips changed notification (used by undo system)
     */
    void forceNotifyClipsChanged();

    /**
     * @brief Force a clip property changed notification for a specific clip
     * Used by commands that directly modify clip data without going through ClipManager methods
     */
    void forceNotifyClipPropertyChanged(ClipId clipId);
    void forceNotifyMultipleClipPropertiesChanged(const std::vector<ClipId>& clipIds);

    /**
     * @brief Copy an audio clip source into the project external-edits folder and open it.
     */
    bool editAudioClipSourceInExternalEditor(ClipId clipId, juce::String& errorMessage);

    /**
     * @brief Duplicate a clip (places copy right after original)
     * @return The ID of the new clip
     */
    ClipId duplicateClip(ClipId clipId);

    /**
     * @brief Duplicate a clip at a specific beat position.
     */
    ClipId duplicateClipAtBeats(ClipId clipId, double startBeat, TrackId trackId = INVALID_TRACK_ID,
                                double tempo = 0.0);

    /**
     * @brief Duplicate a clip at a specific timeline-second position.
     * @param clipId The clip to duplicate
     * @param startTime Where to place the duplicate
     * @param trackId Track for the duplicate (INVALID_TRACK_ID = same track)
     * @return The ID of the new clip
     */
    ClipId duplicateClipAt(ClipId clipId, double startTime, TrackId trackId = INVALID_TRACK_ID,
                           double tempo = 120.0);

    // ========================================================================
    // Ghost clips (link groups)
    // ========================================================================

    /**
     * @brief Duplicate a clip as a ghost: the copy joins the original's link
     * group (creating one if the original is unlinked), so content edits
     * mirror between all members. The copy keeps the original's name.
     * @return The ID of the new clip
     */
    ClipId duplicateClipAsGhost(ClipId clipId);

    /** @brief Ghost-duplicate at a specific beat position (drag-copy path). */
    ClipId duplicateClipAsGhostAtBeats(ClipId clipId, double startBeat,
                                       TrackId trackId = INVALID_TRACK_ID, double tempo = 0.0);

    /** @brief Detach a clip from its link group (no-op if unlinked). */
    void makeClipUnique(ClipId clipId);

    /**
     * @brief IDs of the other members of this clip's link group.
     * Empty when unlinked or when the clip is the group's only member.
     */
    std::vector<ClipId> getLinkGroupSiblings(ClipId clipId) const;

    /**
     * @brief True when the clip shares a link group with at least one other
     * clip (drives ghost visuals and content propagation).
     */
    bool isGhostClip(ClipId clipId) const;

    /**
     * @brief 1-based display index of the clip within its link group,
     * ordered by clip id (creation order). 0 when the clip is unlinked or
     * the group's only member. Display-only — never stored.
     */
    int getLinkGroupIndex(ClipId clipId) const;

    // ========================================================================
    // Clip Manipulation
    // ========================================================================

    /** @brief Move clip to a new start beat. */
    void moveClipBeats(ClipId clipId, double newStartBeat, double tempo = 0.0);

    /** @brief Move clip to a new timeline-second start. */
    void moveClip(ClipId clipId, double newStartTime, double tempo = 0.0);

    /**
     * @brief Move clip to a different track
     */
    void moveClipToTrack(ClipId clipId, TrackId newTrackId);

    /** @brief Resize clip to a new beat length. */
    void resizeClipBeats(ClipId clipId, double newLengthBeats, bool fromStart = false,
                         double tempo = 0.0);

    /** @brief Resize clip to a new timeline-second length. */
    void resizeClip(ClipId clipId, double newLength, bool fromStart = false, double tempo = 120.0);

    /** @brief Split a clip at a specific beat position. */
    ClipId splitClipAtBeat(ClipId clipId, double splitBeat, double tempo = 0.0);

    /** @brief Split a clip at a specific timeline-second position. */
    ClipId splitClip(ClipId clipId, double splitTime, double tempo = 120.0);

    /** @brief Trim clip to a beat range. */
    void trimClipBeats(ClipId clipId, double newStartBeat, double newLengthBeats,
                       double tempo = 0.0);

    /** @brief Trim clip to a timeline-second range. */
    void trimClip(ClipId clipId, double newStartTime, double newLength, double tempo = 0.0);

    // ========================================================================
    // Clip Properties
    // ========================================================================

    void setClipName(ClipId clipId, const juce::String& name);
    void setClipColour(ClipId clipId, juce::Colour colour);
    /** @brief Enable/disable a clip (#1736). Disabled clips do not play. */
    void setClipEnabled(ClipId clipId, bool enabled);
    void setClipLoopEnabled(ClipId clipId, bool enabled, double projectBPM = 120.0);
    void setClipMidiOffset(ClipId clipId, double offsetBeats);
    void setClipLaunchMode(ClipId clipId, LaunchMode mode);
    void setClipLaunchQuantize(ClipId clipId, LaunchQuantize quantize);
    void setClipFollowAction(ClipId clipId, FollowAction action);
    void setClipFollowActionDelayBeats(ClipId clipId, double delayBeats);
    void setClipFollowActionLoopCount(ClipId clipId, int loopCount);

    // Warp
    /** @brief Enable or disable warp markers on an audio clip */
    void setClipWarpEnabled(ClipId clipId, bool enabled);

    // -- Audio loop / offset setters (TE-aligned model) --
    //
    // Each setter has a deliberately narrow scope. If you need a composite
    // "drag the whole loop region" operation that also resets phase, call
    // relocateLoopRegion — that's the only setter that intentionally
    // touches a sibling field beyond the one its name advertises.
    //
    // The bpm argument on the audio setters is only consulted when
    // autoTempo is enabled and the source-interpretation BPM is missing;
    // it backfills the seconds-to-beats conversion. autoTempo clips with
    // a known source BPM ignore it.

    /** @brief Set the offset (playback start position) in the audio file
     *         (source-time seconds). Does NOT touch loop fields. */
    void setOffset(ClipId clipId, double offset);

    /** @brief Set the loop phase — i.e. set offset = loopStart + phase.
     *         Audio + loop-active clips only. Does NOT touch loopStart. */
    void setLoopPhase(ClipId clipId, double phase);

    /** @brief Set the loop region start (source-time seconds). Does NOT
     *         touch offset / phase. */
    void setLoopStart(ClipId clipId, double loopStart, double bpm = 120.0);

    /** @brief Set the loop region length (source-time seconds). Does NOT
     *         touch offset / phase / loop start. */
    void setLoopLength(ClipId clipId, double loopLength, double bpm = 120.0);

    /** @brief Set MIDI loop region start in beats. Does NOT touch offset / phase. */
    void setMidiLoopStartBeats(ClipId clipId, double loopStartBeats, double bpm = 120.0);

    /** @brief Set MIDI loop region length in beats. Does NOT touch offset / phase / loop start. */
    void setMidiLoopLengthBeats(ClipId clipId, double loopLengthBeats, double bpm = 120.0);

    /** @brief Composite operation: relocate the loop region (start + length)
     *         AND snap phase to 0 by setting offset = loopStart whenever
     *         loopStart actually moved.
     *
     *         Use this for editor drag gestures where the user is
     *         relocating the whole loop region as one unit. Use the
     *         narrower setLoopStart / setLoopLength setters for inspector
     *         spinner edits where phase must be preserved. */
    void relocateLoopRegion(ClipId clipId, double loopStart, double loopLength, double bpm = 120.0);
    /** @brief Set the clip timeline length in beats (autoTempo mode only) */
    void setLengthBeats(ClipId clipId, double beats, double bpm);

    // =====================================================================
    // Session audio-clip canonical update path (issue #1157)
    //
    // For session/autoTempo audio clips, ClipInfo holds two roles:
    //   - SOURCE INTERPRETATION — AudioClipModel::interpretation. The file's
    //     musical reading, user-correctable and never clip placement.
    //   - USER INTENT — lengthBeats (timeline beats the clip occupies on
    //     the session/timeline), loopStartBeats / loopLengthBeats (sub-loop
    //     region in source-beat domain), offsetBeats, startBeats. The beat
    //     slider edits lengthBeats and never touches source interpretation.
    //
    // Time-domain fields (length, startTime, offset, loopStart, loopLength)
    // are DERIVED inside applyAudioClipBeats and must not be set directly
    // by callers in this path. speedRatio is forced to 1.0.
    // =====================================================================
    struct AudioClipBeatsUpdate {
        std::optional<double> sourceDurationSeconds;
        std::optional<double> interpretationBpm;
        std::optional<double> interpretationTotalBeats;
        bool lockInterpretationTotalBeats = false;
        std::optional<double> lengthBeats;
        std::optional<double> loopStartBeats;
        std::optional<double> loopLengthBeats;
        std::optional<double> offsetBeats;
        std::optional<double> startBeats;
    };

    /** @brief Apply a partial canonical update to a session/autoTempo audio
     *         clip and atomically recompute every derived field. Single
     *         update path for inspector BPM edit, beat-length slider, and
     *         BPM-detection callbacks. No-op for non-autoTempo / non-audio
     *         clips. */
    void applyAudioClipBeats(ClipId clipId, const AudioClipBeatsUpdate& update, double projectBPM);

    /** @brief Persist a user-asserted BPM for the clip's source file back
     *         to the media DB. Prefer saveClipToLibrary for UI entry points
     *         so all library-editable properties commit atomically. */
    void recordUserBpm(ClipId clipId, double bpm);

    /** @brief Persist a user-asserted key root for the clip's source file
     *         back to the media DB. Prefer saveClipToLibrary for UI entry
     *         points so all library-editable properties commit atomically. */
    void recordUserKey(ClipId clipId, const std::string& root);

    [[nodiscard]] bool canSaveClipToLibrary(ClipId clipId) const;

    [[nodiscard]] bool saveClipToLibrary(
        ClipId clipId, std::optional<std::vector<WarpMarker>> warpMarkers = std::nullopt);

    /** @brief Refresh the seconds-domain cache (length, startTime, offset,
     *         loopStart, loopLength) on a beat-authoritative clip from its
     *         canonical beat fields. No-op for time-authoritative clips.
     *
     *  Called by applyAudioClipBeats and by TimelineController on project-BPM
     *  change. Does NOT notify listeners — caller's responsibility. */
    void refreshDerivedSeconds(ClipId clipId, double projectBPM);

    /** @brief Enable/disable auto-tempo (beat-locked) mode for an audio clip */
    void setAutoTempo(ClipId clipId, bool enabled, double bpm);
    /** @brief Set the playback speed ratio (1.0 = original, 2.0 = double speed) - TE:
     * Clip::speedRatio */
    void setSpeedRatio(ClipId clipId, double speedRatio);
    /** @brief Set the time-stretch algorithm mode for an audio clip */
    void setTimeStretchMode(ClipId clipId, int mode);

    // Pitch
    void setAutoPitch(ClipId clipId, bool enabled);
    void setAnalogPitch(ClipId clipId, bool enabled);
    void setAutoPitchMode(ClipId clipId, int mode);
    void setPitchChange(ClipId clipId, float semitones);
    void setTranspose(ClipId clipId, int semitones);

    // Beat Detection
    void setAutoDetectBeats(ClipId clipId, bool enabled);
    void setBeatSensitivity(ClipId clipId, float sensitivity);

    // Playback
    void setIsReversed(ClipId clipId, bool reversed);

    // Per-Clip Mix
    void setClipVolumeDB(ClipId clipId, float dB);
    void setClipGainDB(ClipId clipId, float dB);
    void setClipPan(ClipId clipId, float pan);

    // Fades. Seconds, as before #1901 — the fade moved onto the audio event,
    // its units did not.
    void setFadeIn(ClipId clipId, double seconds);
    void setFadeOut(ClipId clipId, double seconds);
    void setFadeInType(ClipId clipId, int type);
    void setFadeOutType(ClipId clipId, int type);
    void setFadeInBehaviour(ClipId clipId, int behaviour);
    void setFadeOutBehaviour(ClipId clipId, int behaviour);
    void setAutoCrossfade(ClipId clipId, bool enabled);

    /// Let this clip play through its overlaps instead of the stack silencing
    /// one side (#2003). Audio and MIDI alike, unlike autoCrossfade.
    void setOverlapPlaysBoth(ClipId clipId, bool playsBoth);

    void setLaunchFadeSamples(ClipId clipId, int samples);

    // ========================================================================
    // Crossfades (#1499)
    //
    // A crossfade IS an overlap: two audio arrangement clips on the same track
    // whose placements partially overlap and whose autoCrossfade flags are set.
    // Playback fades come from TE's auto-crossfade over the overlap region
    // (each clip's stored fadeIn/fadeOut returns when the clips are pulled
    // apart). The geometry is beat-domain and lives in the placements, so it
    // serializes and round-trips with them — there is no separate duration
    // field to keep in sync.
    // ========================================================================

    /// The queries themselves live in ClipFades.hpp, on their own, because they
    /// answer against a lane rather than against the model: the arrangement
    /// asks about a drag it has not committed, and the native engine's clip
    /// snapshot asks without a manager at all (#2034). These names stay so the
    /// call sites that ask the model read as they always did.
    using CrossfadeInfo = ::magda::CrossfadeInfo;
    using EffectiveFades = ::magda::EffectiveFades;

    /// The crossfade covering this clip's start/end edge, if any. Only
    /// returns a value when the overlap qualifies (both clips audio,
    /// arrangement, autoCrossfade on, partial overlap).
    std::optional<CrossfadeInfo> getCrossfadeAtStart(ClipId clipId) const;
    std::optional<CrossfadeInfo> getCrossfadeAtEnd(ClipId clipId) const;

    /// The same queries against an explicit set of clips rather than the
    /// committed model, so a drag can show the crossfade it is about to make or
    /// break while the mouse is still down (#2003). The lane must hold the
    /// clips as they would be at that moment, the dragged one included.
    static std::optional<CrossfadeInfo> crossfadeAtStartIn(const std::vector<ClipInfo>& lane,
                                                           ClipId clipId);
    static std::optional<CrossfadeInfo> crossfadeAtEndIn(const std::vector<ClipInfo>& lane,
                                                         ClipId clipId);

    /// Every arrangement clip on one track, which is the unit an overlap is
    /// resolved against.
    std::vector<ClipInfo> arrangementLane(TrackId trackId) const;

    /**
     * @brief The fades a clip really plays with, once the lane is taken into
     *        account (#2003).
     *
     * Its own stored fades, replaced at either edge by the overlap AUTO-XFADE
     * turns into a fade, and clamped so the two never sum past the clip — the
     * same clamp TE applies, so what is drawn is what is heard. One call for
     * the arrangement view and for the engine, because a fade drawn differently
     * from the one played is the bug this replaces.
     *
     * @param lane   The clips as they are at this moment, this one included —
     *               the committed lane, or a previewed one mid-drag.
     */
    static EffectiveFades effectiveFadesIn(const std::vector<ClipInfo>& lane, ClipId clipId,
                                           double bpm);

    /// The same against the committed model.
    EffectiveFades getEffectiveFades(ClipId clipId, double bpm) const;

    /// The audio clip abutting/overlapping this clip's start (previous) or
    /// end (next) that a crossfade could be created with — regardless of the
    /// autoCrossfade flags. INVALID_CLIP_ID if none.
    ClipId findCrossfadeNeighbour(ClipId clipId, bool atStart) const;

    /**
     * @brief Set the crossfade overlap region between two clips — the core
     *        beats-authoritative crossfade edit.
     *
     * Moves the left clip's right edge to endBeat and the right clip's left
     * edge to startBeat (content-anchored, via the container resize helpers),
     * clamped so neither clip swallows the other and neither runs out of
     * source material. Enables autoCrossfade on both clips when the resulting
     * overlap is non-empty. startBeat == endBeat butts the joint (removes the
     * crossfade; the flags stay).
     *
     * @return false when the pair is not crossfade-capable (different track,
     *         non-audio, no joint between them).
     */
    bool setCrossfadeRegionBeats(ClipId leftId, ClipId rightId, double startBeat, double endBeat,
                                 double tempo = 0.0);

    /**
     * @brief Create/resize a crossfade centred on the joint (current overlap
     *        centre, or the touch point for abutting clips).
     *        durationBeats == 0 removes it.
     */
    bool setCrossfadeBeats(ClipId leftId, ClipId rightId, double durationBeats, double tempo = 0.0);

    // Channels
    void setLeftChannelActive(ClipId clipId, bool active);
    void setRightChannelActive(ClipId clipId, bool active);

    // Groove/Shuffle/Swing (MIDI clips)
    void setGrooveTemplate(ClipId clipId, const juce::String& templateName);
    void setGrooveStrength(ClipId clipId, float strength);

    // Per-clip grid settings (MIDI editor)
    void setClipGridSettings(ClipId clipId, bool autoGrid, int numerator, int denominator);
    void setClipSnapEnabled(ClipId clipId, bool enabled);
    void setClipMidiEditorRowHeight(ClipId clipId, int rowHeight);

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
                          double originalSpeedRatio, double bpm = 0.0);

    /**
     * @brief Stretch audio from right edge (editor operation)
     * @param newLength New timeline length
     * @param oldLength Original timeline length at drag start
     * @param originalSpeedRatio Original speed ratio at drag start
     */
    void stretchAudioRight(ClipId clipId, double newLength, double oldLength,
                           double originalSpeedRatio, double bpm = 0.0);

    // MIDI-specific
    bool addMidiNote(ClipId clipId, const MidiNote& note);
    void removeMidiNote(ClipId clipId, int noteIndex);
    void clearMidiNotes(ClipId clipId);
    // Replace a note's pitch glide points (sorted by beat, clamped by caller)
    void setMidiNotePitchExpression(ClipId clipId, size_t noteIndex,
                                    std::vector<MidiPitchExpressionPoint> points);

    // Chord annotations
    void addChordAnnotation(ClipId clipId, const ClipInfo::ChordAnnotation& annotation);
    void removeChordAnnotation(ClipId clipId, size_t index);
    void clearChordAnnotations(ClipId clipId);

    // ========================================================================
    // Access
    // ========================================================================

    /**
     * @brief Get all arrangement clips (timeline-based)
     */
    std::vector<ClipInfo> getArrangementClips() const;

    /**
     * @brief Get all session clips (scene-based)
     */
    std::vector<ClipInfo> getSessionClips() const;

    /**
     * @brief Get all clips (both arrangement and session)
     */
    std::vector<ClipInfo> getClips() const;

    ClipInfo* getClip(ClipId clipId);
    const ClipInfo* getClip(ClipId clipId) const;

    /**
     * @brief Get all clips on a specific track
     */
    std::vector<ClipId> getClipsOnTrack(TrackId trackId) const;
    std::vector<ClipId> getClipsOnTrack(TrackId trackId, ClipView view) const;

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

    /** The last session clip that was triggered via triggerClip(). Persists
        across transport stop so Record can re-trigger it. */
    ClipId getLastTriggeredSessionClip() const {
        return lastTriggeredSessionClipId_;
    }

    // ========================================================================
    // Clipboard Operations
    // ========================================================================

    /**
     * @brief Copy selected clips to clipboard
     * @param clipIds The clips to copy
     */
    void copyToClipboard(const std::unordered_set<ClipId>& clipIds);

    /**
     * @brief Copy the overlapping portions of clips within a beat range to
     *        clipboard — beats-authoritative entry point.
     *
     * Clipboard positions are beat-domain; overlap, trimming and the paste
     * reference anchor are all computed in beats. bpm is used only to derive
     * source-domain seconds (audio offset/loop) at the boundary.
     * @param startBeat Start of range (beats)
     * @param endBeat End of range (beats)
     * @param trackIds Tracks to copy from (empty = all arrangement tracks)
     */
    void copyBeatRangeToClipboard(double startBeat, double endBeat,
                                  const std::vector<TrackId>& trackIds, double tempoBPM = 120.0);

    /**
     * @brief Copy a time range to clipboard from timeline seconds.
     *
     * Thin shim around copyBeatRangeToClipboard for callers whose natural unit
     * is still seconds (bridge/UI sites carrying the transitional seconds cache).
     */
    void copyTimeRangeToClipboard(double startTime, double endTime,
                                  const std::vector<TrackId>& trackIds, double tempoBPM = 120.0);

    /**
     * @brief Paste clips from clipboard at a beat position — beats-authoritative.
     * @param pasteBeat Timeline position to paste at (beats)
     * @param targetTrackId Track to paste on (INVALID_TRACK_ID = use original tracks)
     * @return IDs of the newly created clips
     */
    std::vector<ClipId> pasteFromClipboardBeats(double pasteBeat,
                                                TrackId targetTrackId = INVALID_TRACK_ID,
                                                ClipView targetView = ClipView::Arrangement,
                                                int targetSceneIndex = -1);

    /**
     * @brief Paste clips from clipboard at a timeline-seconds position.
     *
     * Thin shim around pasteFromClipboardBeats for seconds-domain callers.
     * @param pasteTime Timeline position to paste at (seconds)
     */
    std::vector<ClipId> pasteFromClipboard(double pasteTime,
                                           TrackId targetTrackId = INVALID_TRACK_ID,
                                           ClipView targetView = ClipView::Arrangement,
                                           int targetSceneIndex = -1);

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
     * @brief Beat span of the clipboard contents (max clip end - reference anchor).
     *        0 if the clipboard is empty. Used to size a ripple-insert on paste.
     */
    double getClipboardBeatSpan() const;

    /**
     * @brief True when clipboard clips have no source track and paste must supply one.
     */
    bool clipboardRequiresTargetTrack() const;

    /**
     * @brief Clear clipboard
     */
    void clearClipboard();

    /**
     * @brief Set the clip clipboard from external MIDI notes, such as a sequencer pattern.
     */
    void setMidiClipClipboard(std::vector<MidiNote> notes, juce::String name = "Sequencer Pattern",
                              double lengthBeats = 0.0);

    // ========================================================================
    // Note Clipboard Operations (for MIDI note copy/paste)
    // ========================================================================

    /**
     * @brief Copy selected notes to the note clipboard
     * Notes are stored with startBeat normalised (earliest = 0)
     */
    void copyNotesToClipboard(ClipId clipId, const std::vector<size_t>& noteIndices);

    /**
     * @brief Check if note clipboard has notes
     */
    bool hasNotesInClipboard() const;

    /**
     * @brief Get notes from the clipboard
     */
    const std::vector<MidiNote>& getNoteClipboard() const;

    /**
     * @brief Get the original earliest startBeat before normalisation
     */
    double getNoteClipboardMinBeat() const;

    /**
     * @brief Set note clipboard directly from external notes (e.g. step sequencer pattern export).
     * Notes are stored as-is — caller is responsible for normalisation if desired.
     */
    void setNoteClipboard(std::vector<MidiNote> notes);

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
     * @brief Suspend and coalesce per-clip property notifications.
     *
     * Nestable. While any suspension is active, notifyClipPropertyChanged()
     * records the clip id into a set instead of firing listeners. When the
     * outermost suspension ends, a single clipPropertiesChanged(ids) is
     * fired covering everything that changed during the batch.
     *
     * Notes about clipsChanged() (structural changes) are NOT coalesced and
     * still fire immediately.
     *
     * Intended for bulk mutations like AI-driven note generation where
     * firing per-note would cause O(n) full TE sequence rebuilds plus
     * O(n) UI repaints.
     */
    void beginBatch();
    void endBatch();

    /// RAII helper for beginBatch/endBatch.
    class BatchScope {
      public:
        BatchScope() {
            ClipManager::getInstance().beginBatch();
        }
        ~BatchScope() {
            try {
                ClipManager::getInstance().endBatch();
            } catch (...) {
                // best-effort notification flush; a listener throw must not terminate
            }
        }
        BatchScope(const BatchScope&) = delete;
        BatchScope& operator=(const BatchScope&) = delete;
    };

    /// Test-only guard for model assertions that must not drive engine/UI listeners.
    class ScopedListenerMuteForTests {
      public:
        ScopedListenerMuteForTests();
        ~ScopedListenerMuteForTests();
        ScopedListenerMuteForTests(const ScopedListenerMuteForTests&) = delete;
        ScopedListenerMuteForTests& operator=(const ScopedListenerMuteForTests&) = delete;

      private:
        std::vector<ClipManagerListener*> savedListeners_;
    };

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

    /**
     * @brief Settle a lane after placing/moving a dominant clip
     *
     * The dominant goes to the top of its lane's stack and owns the span it
     * covers, but nothing underneath is cut for it (#2003): covered clips keep
     * their placement and content, and computeAudibleSpans decides what each of
     * them plays, so moving the dominant away fills the gap by itself.
     *
     * Nothing else changes the model: no trim, no split, no delete, at any
     * overlap shape. A drop landing strictly inside another audio clip used to
     * split it into head / covered slice / tail, because the Tracktion mirror
     * holds one engine clip per model clip and cannot express a hole; the
     * native engine carries silenced ranges on the clip snapshot instead
     * (#1890), so that case keeps whole clips too.
     * Called internally by move methods and explicit opt-in creation paths.
     */
    void resolveOverlaps(ClipId dominantClipId);

    /// Reset a looped clip's length to its base loop length and disable looping
    void resetLoopedClipLength(ClipInfo& clip);

    /// Move every event off @p from and onto @p to. Used when a relink target
    /// turns out to be pooled already, so one file keeps one source.
    void repointEventsToSource(SourceId from, SourceId to);

    /// Record the file behind every clipboard event, so a paste after a
    /// project switch resolves by path rather than by a renumbered id.
    void stashClipboardSourcePaths();

    /// Path a clipboard event should paste from.
    juce::String clipboardSourcePathFor(const AudioEvent& event) const;

  private:
    ClipManager();
    ~ClipManager() = default;

    /// Rescale every event on @p sourceId whose source-domain positions were
    /// expressed at @p oldRate. Installed on SourcePool as its rate-change
    /// handler, so resolving or relinking a file cannot move clip positions.
    void rescaleEventsForSourceRate(SourceId sourceId, double oldRate, double newRate);

    double findNonOverlappingStartBeats(TrackId trackId, double desiredStartBeats,
                                        double lengthBeats, ClipView view) const;

    // How far (in timeline beats) an audio clip's edge can extend before it
    // runs out of source material. Left = earlier than its current start
    // (bounded by the source read offset), right = past its current end
    // (bounded by the source duration). Unbounded (infinity) for looping
    // clips and when the source duration is unknown.
    double availableLeftExtensionBeats(const ClipInfo& clip, double bpm) const;
    double availableRightExtensionBeats(const ClipInfo& clip, double bpm) const;

    // Unified clip storage — ClipView is a property, not storage identity
    std::unordered_map<ClipId, ClipInfo> clips_;

    // Fast (TrackId, sceneIndex) -> ClipId lookup for session-view slots.
    // Maintained by every code path that creates/deletes a session clip or
    // changes its trackId/sceneIndex. Read-only consumers go through
    // getClipInSlot() — never poke the map directly.
    //
    // Why: getClipInSlot is called O(tracks * scenes) per SessionView paint
    // and from multiple drag/drop and command paths. Scanning all clips on
    // every call doesn't scale to large grids.
    std::unordered_map<uint64_t, ClipId> sessionSlotIndex_;

    static uint64_t makeSessionSlotKey(TrackId trackId, int sceneIndex) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(trackId)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(sceneIndex));
    }
    void addToSessionSlotIndex(const ClipInfo& clip);
    void removeFromSessionSlotIndex(const ClipInfo& clip);

    // Clipboard storage
    std::vector<ClipInfo> clipboard_;
    /// File path per source id referenced by the clipboard, captured at copy
    /// time. The clipboard deliberately survives project switches, but an
    /// event carries only a SourceId and loading a project clears and
    /// renumbers the pool, so a stored id would resolve against the new
    /// project's table: a collision pastes the wrong file, a miss pastes
    /// nothing. Paths do not collide.
    std::unordered_map<SourceId, juce::String> clipboardSourcePaths_;
    double clipboardReferenceBeat_ = 0.0;  // Beats; anchor for maintaining relative paste positions

    // Note clipboard storage
    std::vector<MidiNote> noteClipboard_;
    double noteClipboardMinBeat_ = 0.0;  // Original earliest startBeat before normalisation

    std::vector<ClipManagerListener*> listeners_;

    // Batch notification state (see beginBatch/endBatch).
    int batchDepth_ = 0;
    std::vector<ClipId> batchedClipIds_;  // kept in insertion order, deduped

    int nextClipId_ = 1;
    int nextLinkGroupId_ = 1;
    int nextStackOrder_ = 1;
    ClipId selectedClipId_ = INVALID_CLIP_ID;
    ClipId lastTriggeredSessionClipId_ = INVALID_CLIP_ID;

    /// Put an arrangement clip on top of its lane (#2003). Every path that
    /// places or moves a clip goes through here, so "on top" is whatever the
    /// user touched last — and the clip they just dropped is the one that owns
    /// the span it landed on.
    void bringToFrontOfStack(ClipInfo& clip);

    /// Assign a fresh link group to the clip if it has none. Returns the group id.
    int ensureLinkGroup(ClipInfo& clip);

    /// Mirror the clip's shared content to all link-group siblings (no-op when
    /// unlinked, or when the notification didn't touch shared content).
    /// Returns the sibling ids that were updated so the notify funnel can
    /// include them in the same pass.
    std::vector<ClipId> propagateLinkGroupContent(ClipId clipId);

    // Fast linkGroupId -> members lookup (members sorted by ClipId, i.e.
    // creation order). isGhostClip / getLinkGroupIndex are called several
    // times per clip paint, so scanning all clips made full repaints O(n^2).
    // Maintained on every add / remove / link change inside the manager;
    // silent writes from outside (undo snapshot restores assign whole
    // ClipInfo structs) are reconciled in the notify funnels: per-clip in
    // notifyClipPropertyChanged, full rebuild in notifyClipsChanged. Project
    // load funnels through clearAllClips + restoreClip, which rebuild it.
    std::unordered_map<int, std::vector<ClipId>> linkGroupMembers_;
    std::unordered_map<ClipId, int> indexedGroupOf_;  // group recorded per clip

    /// Record `clipId` as belonging to `groupId` (0 = unlinked), moving it
    /// between member buckets as needed. No-op when already recorded.
    void indexClipGroup(ClipId clipId, int groupId);
    /// Rebuild both maps from clips_ (silent-write reconciliation).
    void rebuildLinkGroupIndex();

    // Notification helpers (public so scheduler can emit state changes)
  public:
    void notifyClipPlaybackStateChanged(ClipId clipId);

  private:
    void notifyClipsChanged();
    void notifyClipPropertyChanged(ClipId clipId);
    void notifyClipSelectionChanged(ClipId clipId);
    void notifyClipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request);

    // Clamp audio clip properties (offset, loopStart, loopLength) to file bounds
    void sanitizeAudioClip(ClipInfo& clip);

    // Helper to generate unique clip name
    juce::String generateClipName(ClipType type) const;
};

}  // namespace magda
