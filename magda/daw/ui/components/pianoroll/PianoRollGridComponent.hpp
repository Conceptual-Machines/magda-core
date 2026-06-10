#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "NoteComponent.hpp"
#include "NoteGridHost.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipTypes.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TempoUtils.hpp"

namespace magda {

/**
 * @brief Scrollable grid component containing MIDI notes
 *
 * Handles:
 * - Grid background rendering (beat lines, note rows)
 * - Note component management (create, update, delete)
 * - Double-click to add notes
 * - Grid snap settings
 * - Coordinate conversion (beat <-> pixel, noteNumber <-> y)
 */
class PianoRollGridComponent : public juce::Component,
                               public juce::DragAndDropTarget,
                               public juce::FileDragAndDropTarget,
                               public juce::Timer,
                               public juce::ApplicationCommandTarget,
                               public ClipManagerListener,
                               public NoteGridHost {
  public:
    PianoRollGridComponent();
    ~PianoRollGridComponent() override;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // Timer (for pending chord blink)
    void timerCallback() override;

    // Mouse handling
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Keyboard handling
    bool keyPressed(const juce::KeyPress& key) override;

    // ApplicationCommandTarget: context-scoped commands (#25). When the grid is
    // focused these win over the global handlers; getNextCommandTarget() chains
    // back to the global target so non-piano-roll commands still fall through.
    juce::ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

    // Set the clip(s) to display/edit
    void setClip(ClipId clipId);  // Single clip (backward compatibility)
    void setClips(TrackId trackId, const std::vector<ClipId>& selectedClipIds,
                  const std::vector<ClipId>& allClipIds);  // Multi-clip mode
    ClipId getClipId() const {
        return clipId_;
    }
    const std::vector<ClipId>& getClipIds() const {
        return clipIds_;
    }
    const std::vector<ClipId>& getSelectedClipIds() const {
        return selectedClipIds_;
    }
    TrackId getTrackId() const {
        return trackId_;
    }

    // Multi-track overlay (ghost notes from other tracks, #1281). Overlay
    // tracks render as dimmed, non-interactive notes in their track colour.
    void setOverlayTracks(std::vector<TrackId> trackIds);
    const std::vector<TrackId>& getOverlayTracks() const {
        return overlayTrackIds_;
    }

    // Zoom settings
    void setPixelsPerBeat(double ppb);
    double getPixelsPerBeat() const override {
        return pixelsPerBeat_;
    }

    void setNoteHeight(int height);
    int getNoteHeight() const override {
        return noteHeight_;
    }

    // Left padding (for alignment with ruler if needed)
    void setLeftPadding(int padding);
    int getLeftPadding() const {
        return leftPadding_;
    }

    // Clip position on timeline (for absolute positioning)
    void setClipStartBeats(double startBeats);
    double getClipStartBeats() const {
        return clipStartBeats_;
    }

    // Clip boundary marker (shows where clip content ends)
    void setClipLengthBeats(double lengthBeats);
    double getClipLengthBeats() const {
        return clipLengthBeats_;
    }

    // Display mode (relative = notes at beat 0, absolute = notes at clipStart + beat)
    void setRelativeMode(bool relative);
    bool isRelativeMode() const {
        return relativeMode_;
    }

    // Timeline length (for drawing grid lines to the full timeline extent)
    void setTimelineLengthBeats(double lengthBeats);
    double getTimelineLengthBeats() const {
        return timelineLengthBeats_;
    }

    // Loop region markers (shows loop boundaries on the grid)
    void setLoopRegion(double offsetBeats, double lengthBeats, bool enabled);
    double getLoopOffsetBeats() const {
        return loopOffsetBeats_;
    }
    double getLoopLengthBeats() const {
        return loopLengthBeats_;
    }
    bool isLoopEnabled() const {
        return loopEnabled_;
    }

    // Phase marker preview (overrides clip->midiOffset during drag)
    void setPhasePreview(double beats, bool active);

    // Pitch glide (MPE pitch expression) overlay editing mode.
    // When enabled, mouse interaction edits per-note pitch curves instead of notes.
    void setPitchExpressionMode(bool enabled);
    bool isPitchExpressionMode() const {
        return pitchExpressionMode_;
    }

    // Playhead position (for drawing playhead line during playback)
    void setPlayheadPosition(double positionSeconds);
    double getPlayheadPosition() const {
        return playheadPosition_;
    }

    // Edit cursor position (for drawing blinking edit cursor line)
    void setEditCursorPosition(double positionSeconds, bool blinkVisible);

    // Grid snap settings
    void setGridResolutionBeats(double beats);
    double getGridResolutionBeats() const {
        return gridResolutionBeats_;
    }
    void setSnapEnabled(bool enabled);
    bool isSnapEnabled() const {
        return snapEnabled_;
    }
    void setTimeSignatureNumerator(int numerator);

    // Coordinate conversion
    int beatToPixel(double beat) const;
    double pixelToBeat(int x) const;
    int noteNumberToY(int noteNumber) const;
    int yToNoteNumber(int y) const;

    // NoteGridHost overrides
    juce::Point<int> getGridScreenPosition() const override {
        return localPointToGlobal(juce::Point<int>());
    }
    void updateNotePosition(NoteComponent* note, double beat, int noteNumber,
                            double length) override;
    void setCopyDragPreview(double beat, int noteNumber, double length, juce::Colour colour,
                            bool active, size_t sourceNoteIndex) override;
    void updateSelectedNotePositions(NoteComponent* draggedNote, double beatDelta,
                                     int noteDelta) override;
    void updateSelectedNoteLengths(NoteComponent* draggedNote, double lengthDelta) override;
    void updateSelectedNoteLeftResize(NoteComponent* draggedNote, double lengthDelta) override;

    // Refresh note components from clip data
    void refreshNotes();

    // Request a note to be selected after the next refresh
    void selectNoteAfterRefresh(ClipId clipId, int noteIndex);

    std::function<void(int, const juce::MouseWheelDetails&)> onVerticalZoomRequested;

    // Update visual selection to match SelectionManager state
    void syncSelectionFromManager();

    // ClipManagerListener
    void clipsChanged() override {}
    void clipPropertyChanged(ClipId clipId) override;
    void clipSelectionChanged(ClipId /*clipId*/) override {}

    // Callbacks for parent to handle undo/redo
    std::function<void(ClipId, double, int, double, int)>
        onNoteAdded;  // clipId, beat, noteNumber, length, velocity
    std::function<void(ClipId, size_t, double, int)>
        onNoteMoved;  // clipId, index, newBeat, newNoteNumber
    std::function<void(ClipId, size_t, double, int)>
        onNoteCopied;  // clipId, index, destBeat, destNoteNumber
    std::function<void(ClipId, size_t, double)> onNoteResized;  // clipId, index, newLength
    std::function<void(ClipId, size_t)> onNoteDeleted;          // clipId, index
    std::function<void(ClipId, size_t, bool)> onNoteSelected;   // clipId, index, isAdditive
    std::function<void(ClipId, size_t)> onNoteRangeSelected;    // clipId, index

    // Callback when note selection changes (e.g. after lasso, deselect-all)
    // Provides the full set of currently selected note indices for the primary clip
    std::function<void(ClipId, std::vector<size_t>)> onNoteSelectionChanged;
    std::function<void(const std::set<int>&)> onSelectedPitchRowsChanged;

    // Callback for drag preview (for syncing velocity lane position)
    std::function<void(ClipId, size_t, double, bool)>
        onNoteDragging;  // clipId, index, previewBeat, isDragging

    // Callbacks for multi-note operations (single undo step)
    std::function<void(ClipId, std::vector<MoveMultipleMidiNotesCommand::NoteMove>)>
        onMultipleNotesMoved;
    std::function<void(ClipId, std::vector<std::pair<size_t, double>>)> onMultipleNotesResized;
    std::function<void(ClipId, std::vector<MoveMultipleMidiNotesCommand::NoteMove>,
                       std::vector<std::pair<size_t, double>>)>
        onLeftResizeMultipleNotes;  // compound move+resize for left-edge resize

    // Callbacks for edit operations from context menu
    std::function<void(ClipId, std::vector<size_t>, QuantizeMode, double gridBeats)>
        onQuantizeNotes;
    std::function<void(ClipId, std::vector<size_t>)> onCopyNotes;
    std::function<void(ClipId)> onPasteNotes;
    std::function<void(ClipId, std::vector<size_t>)> onDuplicateNotes;
    std::function<void(ClipId, std::vector<size_t>)> onDeleteNotes;

    // Edit cursor click on grid (Alt+click) — position in seconds
    std::function<void(double)> onEditCursorSet;

    // Chord block drop — clipId, beat position, notes (noteNumber + velocity pairs), chord name,
    // length
    std::function<void(ClipId, double, double, std::vector<std::pair<int, int>>, juce::String)>
        onChordDropped;

    // Pitch glide edited — clipId, noteIndex, new point set (sorted by beat)
    std::function<void(ClipId, size_t, std::vector<MidiPitchExpressionPoint>)>
        onPitchExpressionChanged;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    // FileDragAndDropTarget — accept .mid file drops (chord blocks, exported patterns)
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

  private:
    ClipId clipId_ = INVALID_CLIP_ID;      // Primary selected clip (for backward compatibility)
    std::vector<ClipId> selectedClipIds_;  // All selected clips (editable)
    std::vector<ClipId> clipIds_;          // All clips being displayed
    TrackId trackId_ = INVALID_TRACK_ID;

    // Tracks whose MIDI renders as a ghost overlay (paint-only, never interactive)
    std::vector<TrackId> overlayTrackIds_;
    void paintOverlayNotes(juce::Graphics& g);

    // Note range
    static constexpr int MIN_NOTE = 0;    // C-2
    static constexpr int MAX_NOTE = 127;  // G9
    static constexpr int NOTE_COUNT = MAX_NOTE - MIN_NOTE + 1;

    // Left padding (0 by default for piano roll since keyboard provides context)
    int leftPadding_ = 0;

    // Zoom settings
    double pixelsPerBeat_ = 50.0;
    int noteHeight_ = 12;

    // Grid snap
    double gridResolutionBeats_ = 0.25;  // Default 1/16 note
    bool snapEnabled_ = true;
    int timeSignatureNumerator_ = DEFAULT_TIME_SIGNATURE_NUMERATOR;

    // Clip position and display mode
    double clipStartBeats_ = 0.0;        // Clip's start position on timeline (in beats)
    double clipLengthBeats_ = 0.0;       // Clip's length (in beats)
    double timelineLengthBeats_ = 64.0;  // Full timeline length (in beats) for drawing grid
    bool relativeMode_ = true;  // true = notes at beat 0, false = notes at absolute position

    // Playhead position (in seconds)
    double playheadPosition_ = -1.0;  // -1 = not playing, hide playhead

    // Edit cursor position (in seconds)
    double editCursorPosition_ = -1.0;  // -1 = hidden
    bool editCursorVisible_ = true;     // blink state

    // Loop region (in beats)
    double loopOffsetBeats_ = 0.0;
    double loopLengthBeats_ = 0.0;
    bool loopEnabled_ = false;
    bool nearPhaseMarker_ = false;  // Mouse is near the phase/offset marker position

    // Phase preview during drag (overrides clip->midiOffset)
    double phasePreviewBeats_ = 0.0;
    bool phasePreviewActive_ = false;

    // Note components
    std::vector<std::unique_ptr<NoteComponent>> noteComponents_;

    // Currently selected note index (or -1 for none)
    int selectedNoteIndex_ = -1;
    std::set<int> selectedPitchRows_;

    // Edit cursor click on grid line
    bool isEditCursorClick_ = false;
    static constexpr int GRID_LINE_HIT_TOLERANCE = 3;
    bool isNearGridLine(int mouseX) const;
    double getNearestGridLineBeat(int mouseX) const;

    // Drag selection (rubber band) state
    bool isDragSelecting_ = false;
    juce::Point<int> dragSelectStart_;
    juce::Point<int> dragSelectEnd_;

    // Shift-drag note creation state
    bool isDrawingNote_ = false;
    ClipId drawingNoteClipId_ = INVALID_CLIP_ID;
    double drawingNoteStartBeat_ = 0.0;  // clip-relative
    double drawingNoteEndBeat_ = 0.0;    // clip-relative
    int drawingNoteNumber_ = 60;
    double defaultNoteLengthBeats_ = 0.0;  // <= 0 follows current grid
    int defaultNoteVelocity_ = 100;
    double getDefaultNoteLengthBeats() const;
    void addDefaultNoteMenuItems(juce::PopupMenu& menu) const;
    bool handleDefaultNoteMenuResult(int result);

    // Pending selection to apply after next refresh
    ClipId pendingSelectClipId_ = INVALID_CLIP_ID;
    int pendingSelectNoteIndex_ = -1;

    // Pending position-based selection for copy operations
    struct PendingSelectPos {
        ClipId clipId;
        double beat;
        int noteNumber;
    };
    std::vector<PendingSelectPos> pendingSelectPositions_;

    // Copy drag ghost preview state
    struct CopyDragGhost {
        double beat = 0.0;
        int noteNumber = 60;
        double length = 1.0;
        juce::Colour colour;
    };
    std::vector<CopyDragGhost> copyDragGhosts_;

    // Chord drop preview state (during DnD drag)
    bool chordDropActive_ = false;
    double chordDropBeat_ = 0.0;  // Snapped beat position during drag

    // Pending chord placement (two-step: drop sets position, click/Enter sets length)
    struct PendingChordDrop {
        ClipId clipId = INVALID_CLIP_ID;
        double startBeat = 0.0;
        double previewEndBeat = 0.0;  // Current mouse position for length preview
        std::vector<std::pair<int, int>> notes;
        juce::String chordName;
        bool active = false;
        bool blinkOn = true;  // Blink state for visual feedback
    };
    PendingChordDrop pendingChord_;
    void confirmPendingChord(double endBeat);
    void cancelPendingChord();

    // Painting helpers
    void paintGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void paintBeatLines(juce::Graphics& g, juce::Rectangle<int> area, double lengthBeats);

    // ------------------------------------------------------------------
    // Pitch glide (MPE pitch expression) overlay
    // ------------------------------------------------------------------
    bool pitchExpressionMode_ = false;

    // Active edit gesture state (working copy committed on mouseUp)
    ClipId expressionClipId_ = INVALID_CLIP_ID;
    size_t expressionNoteIndex_ = 0;
    std::vector<MidiPitchExpressionPoint> expressionWorkingPoints_;
    int expressionDragPointIndex_ = -1;
    bool isExpressionDragging_ = false;

    static constexpr int EXPRESSION_POINT_HIT_RADIUS = 6;
    static constexpr double MAX_PITCH_EXPRESSION_SEMITONES = 48.0;

    void paintPitchExpression(juce::Graphics& g);
    bool handleExpressionMouseDown(const juce::MouseEvent& e);
    void handleExpressionMouseDrag(const juce::MouseEvent& e);
    void handleExpressionMouseUp(const juce::MouseEvent& e);
    bool handleExpressionDoubleClick(const juce::MouseEvent& e);
    void commitExpressionEdit();

    // Coordinate helpers for expression editing
    double expressionRelBeatForX(ClipId clipId, const MidiNote& note, int x) const;
    juce::Point<float> expressionPointToScreen(ClipId clipId, const MidiNote& note,
                                               const MidiPitchExpressionPoint& point) const;
    static double evaluatePitchExpression(const std::vector<MidiPitchExpressionPoint>& points,
                                          double relBeat);

    struct ExpressionHit {
        ClipId clipId = INVALID_CLIP_ID;
        size_t noteIndex = 0;
        int pointIndex = -1;  // -1 = note body (no existing point)

        bool operator==(const ExpressionHit& other) const {
            return clipId == other.clipId && noteIndex == other.noteIndex &&
                   pointIndex == other.pointIndex;
        }
        bool operator!=(const ExpressionHit& other) const {
            return !(*this == other);
        }
    };
    std::optional<ExpressionHit> hitTestExpressionPoint(juce::Point<int> pos) const;
    std::optional<ExpressionHit> hitTestExpressionNote(juce::Point<int> pos) const;

    // Point under the mouse (for the pitch value label)
    std::optional<ExpressionHit> hoveredExpressionPoint_;
    void paintExpressionPointLabel(juce::Graphics& g, const MidiNote& note,
                                   const MidiPitchExpressionPoint& point,
                                   juce::Point<float> screen);

    // Grid snap helper
    double snapBeatToGrid(double beat) const;

    // Note management
    void createNoteComponents();
    void clearNoteComponents();
    void updateNoteComponentBounds();
    void rebuildSelectedPitchRows();

    // Helpers
    struct NoteInsertPosition {
        ClipId clipId = INVALID_CLIP_ID;
        double beat = 0.0;  // clip-relative
        int noteNumber = 60;
    };
    std::optional<NoteInsertPosition> getNoteInsertPosition(juce::Point<int> localPos) const;
    double displayBeatForClipBeat(ClipId clipId, double clipBeat) const;
    double clipBeatForDisplayX(ClipId clipId, int mouseX) const;
    void updateEmptyGridCursor(const juce::ModifierKeys& mods, int mouseX);
    bool isBlackKey(int noteNumber) const;
    juce::Colour getClipColour() const;
    juce::Colour getColourForClip(ClipId clipId) const;
    bool isClipSelected(ClipId clipId) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollGridComponent)
};

}  // namespace magda
