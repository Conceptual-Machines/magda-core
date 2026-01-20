#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "NoteComponent.hpp"
#include "core/ClipInfo.hpp"
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
class PianoRollGridComponent : public juce::Component {
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

    // Set the clip to display/edit
    void setClip(ClipId clipId);
    ClipId getClipId() const {
        return clipId_;
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

    // Grid snap settings
    enum class GridResolution {
        Off,
        Bar,          // 4 beats
        Beat,         // 1 beat (quarter note)
        Eighth,       // 1/8 note
        Sixteenth,    // 1/16 note
        ThirtySecond  // 1/32 note
    };

    void setGridResolution(GridResolution resolution);
    GridResolution getGridResolution() const {
        return gridResolution_;
    }

    // Coordinate conversion
    int beatToPixel(double beat) const;
    double pixelToBeat(int x) const;
    int noteNumberToY(int noteNumber) const;
    int yToNoteNumber(int y) const;

    // Called by NoteComponent to update visual position during drag
    void updateNotePosition(NoteComponent* note, double beat, int noteNumber, double length);

    // Refresh note components from clip data
    void refreshNotes();

    // Callbacks for parent to handle undo/redo
    std::function<void(ClipId, double, int, int)>
        onNoteAdded;  // clipId, beat, noteNumber, velocity
    std::function<void(ClipId, size_t, double, int)>
        onNoteMoved;  // clipId, index, newBeat, newNoteNumber
    std::function<void(ClipId, size_t, double)> onNoteResized;  // clipId, index, newLength
    std::function<void(ClipId, size_t)> onNoteDeleted;          // clipId, index
    std::function<void(ClipId, size_t)> onNoteSelected;         // clipId, index

  private:
    ClipId clipId_ = INVALID_CLIP_ID;

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
    GridResolution gridResolution_ = GridResolution::Sixteenth;

    // Note components
    std::vector<std::unique_ptr<NoteComponent>> noteComponents_;

    // Currently selected note index (or -1 for none)
    int selectedNoteIndex_ = -1;

    // Painting helpers
    void paintGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void paintBeatLines(juce::Graphics& g, juce::Rectangle<int> area, double lengthBeats);

    // Grid snap helper
    double snapBeatToGrid(double beat) const;

    // Get note resolution in beats
    double getGridResolutionBeats() const;

    // Note management
    void createNoteComponents();
    void clearNoteComponents();
    void updateNoteComponentBounds();

    // Helpers
    bool isBlackKey(int noteNumber) const;
    juce::Colour getClipColour() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollGridComponent)
};

}  // namespace magda
