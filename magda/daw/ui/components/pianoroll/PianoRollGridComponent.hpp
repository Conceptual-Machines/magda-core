#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "NoteComponent.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipTypes.hpp"

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
class PianoRollGridComponent : public juce::Component, public ClipManagerListener {
  public:
    PianoRollGridComponent();
    ~PianoRollGridComponent() override;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Mouse handling
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // Keyboard handling
    bool keyPressed(const juce::KeyPress& key) override;

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

    // Zoom settings
    void setPixelsPerBeat(double ppb);
    double getPixelsPerBeat() const {
        return pixelsPerBeat_;
    }

    void setNoteHeight(int height);
    int getNoteHeight() const {
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

    // Playhead position (for drawing playhead line during playback)
    void setPlayheadPosition(double positionSeconds);
    double getPlayheadPosition() const {
        return playheadPosition_;
    }

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

    // Called by NoteComponent to update visual position during drag
    void updateNotePosition(NoteComponent* note, double beat, int noteNumber, double length);

    // Refresh note components from clip data
    void refreshNotes();

    // ClipManagerListener
    void clipsChanged() override {}
    void clipPropertyChanged(ClipId clipId) override;
    void clipSelectionChanged(ClipId clipId) override {}

    // Callbacks for parent to handle undo/redo
    std::function<void(ClipId, double, int, int)>
        onNoteAdded;  // clipId, beat, noteNumber, velocity
    std::function<void(ClipId, size_t, double, int)>
        onNoteMoved;  // clipId, index, newBeat, newNoteNumber
    std::function<void(ClipId, size_t, double)> onNoteResized;  // clipId, index, newLength
    std::function<void(ClipId, size_t)> onNoteDeleted;          // clipId, index
    std::function<void(ClipId, size_t)> onNoteSelected;         // clipId, index

    // Callback for drag preview (for syncing velocity lane position)
    std::function<void(ClipId, size_t, double, bool)>
        onNoteDragging;  // clipId, index, previewBeat, isDragging

  private:
    ClipId clipId_ = INVALID_CLIP_ID;      // Primary selected clip (for backward compatibility)
    std::vector<ClipId> selectedClipIds_;  // All selected clips (editable)
    std::vector<ClipId> clipIds_;          // All clips being displayed
    TrackId trackId_ = INVALID_TRACK_ID;

    // Note range
    static constexpr int MIN_NOTE = 21;   // A0
    static constexpr int MAX_NOTE = 108;  // C8
    static constexpr int NOTE_COUNT = MAX_NOTE - MIN_NOTE + 1;

    // Left padding (0 by default for piano roll since keyboard provides context)
    int leftPadding_ = 0;

    // Zoom settings
    double pixelsPerBeat_ = 50.0;
    int noteHeight_ = 12;

    // Grid snap
    double gridResolutionBeats_ = 0.25;  // Default 1/16 note
    bool snapEnabled_ = true;
    int timeSignatureNumerator_ = 4;

    // Clip position and display mode
    double clipStartBeats_ = 0.0;        // Clip's start position on timeline (in beats)
    double clipLengthBeats_ = 0.0;       // Clip's length (in beats)
    double timelineLengthBeats_ = 64.0;  // Full timeline length (in beats) for drawing grid
    bool relativeMode_ = true;  // true = notes at beat 0, false = notes at absolute position

    // Playhead position (in seconds)
    double playheadPosition_ = -1.0;  // -1 = not playing, hide playhead

    // Loop region (in beats)
    double loopOffsetBeats_ = 0.0;
    double loopLengthBeats_ = 0.0;
    bool loopEnabled_ = false;

    // Note components
    std::vector<std::unique_ptr<NoteComponent>> noteComponents_;

    // Currently selected note index (or -1 for none)
    int selectedNoteIndex_ = -1;

    // Painting helpers
    void paintGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void paintBeatLines(juce::Graphics& g, juce::Rectangle<int> area, double lengthBeats);

    // Grid snap helper
    double snapBeatToGrid(double beat) const;

    // Note management
    void createNoteComponents();
    void clearNoteComponents();
    void updateNoteComponentBounds();

    // Helpers
    bool isBlackKey(int noteNumber) const;
    juce::Colour getClipColour() const;
    juce::Colour getColourForClip(ClipId clipId) const;
    bool isClipSelected(ClipId clipId) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollGridComponent)
};

}  // namespace magda
