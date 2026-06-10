#pragma once

#include <functional>
#include <memory>

#include "MidiEditorContent.hpp"
#include "core/SelectionManager.hpp"

namespace magda {
class PianoRollGridComponent;
class PianoRollKeyboard;
class OctaveLabelStrip;
class SvgButton;
class MidiBridge;
struct MidiNoteEvent;
}  // namespace magda

namespace magda::daw::ui {

/**
 * @brief Piano roll editor for MIDI clips
 *
 * Displays MIDI notes in a piano roll grid layout:
 * - Keyboard on the left showing note names
 * - Note rectangles in the grid representing MIDI notes (interactive)
 * - Time ruler along the top (switchable between absolute/relative)
 */
class PianoRollContent : public MidiEditorContent, public magda::SelectionManagerListener {
  public:
    PianoRollContent();
    ~PianoRollContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::PianoRoll;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::PianoRoll, "Piano Roll", "MIDI note editor", "PianoRoll"};
    }

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void onActivated() override;
    void onDeactivated() override;

    bool wantsHeader() const override {
        return true;
    }

    int getOptimalPanelHeight(int windowHeight) const override {
        return windowHeight * 2 / 3;
    }

    // ClipManagerListener overrides
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;
    void clipDragPreview(magda::ClipId clipId, double previewStartTime,
                         double previewLength) override;

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void multiClipSelectionChanged(const std::unordered_set<magda::ClipId>& clipIds) override;
    void noteSelectionChanged(const magda::NoteSelection& selection) override;

    // TimelineStateListener — not overridden, base handles it

    // Set the clip to edit
    void setClip(magda::ClipId clipId);

    // Timeline mode (overrides base for multi-clip handling)
    void setRelativeTimeMode(bool relative) override;

    // Chord row visibility
    void setChordRowVisible(bool visible);
    bool isChordRowVisible() const {
        return showChordRow_;
    }

  private:
    // MidiEditorContent virtual implementations
    int getLeftPanelWidth() const override {
        return SIDEBAR_WIDTH + ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH;
    }
    void updateGridSize() override;
    void setGridPixelsPerBeat(double ppb) override;
    void setGridPlayheadPosition(double position) override;
    void setGridEditCursorPosition(double positionSeconds, bool visible) override;
    void onScrollPositionChanged(int scrollX, int scrollY) override;
    void onGridResolutionChanged() override;
    void updateGridLoopRegion() override;
    void setGridPhasePreview(double beats, bool active) override;

    // Override velocity lane methods
    void updateVelocityLane() override;
    void onVelocityEdited() override;

    // Layout constants (PianoRoll-specific)
    static constexpr int SIDEBAR_WIDTH = 32;
    static constexpr int ZOOM_STRIP_WIDTH = 16;
    static constexpr int OCTAVE_LABEL_WIDTH = 32;
    static constexpr int KEYBOARD_WIDTH = 60;
    static constexpr int DEFAULT_NOTE_HEIGHT = magda::ClipInfo::DEFAULT_MIDI_EDITOR_ROW_HEIGHT;
    static constexpr int CHORD_ROW_HEIGHT = 24;
    static constexpr int HEADER_HEIGHT = CHORD_ROW_HEIGHT + RULER_HEIGHT;
    static constexpr int MIN_NOTE = 0;    // C-2
    static constexpr int MAX_NOTE = 127;  // G9

    // Vertical zoom limits
    // Piano-roll specific floor — the global ClipInfo min is 6 which lets
    // rows get unreadably thin. Bumped so each key still reads as a key.
    static constexpr int MIN_NOTE_HEIGHT = 10;
    static constexpr int MAX_NOTE_HEIGHT = magda::ClipInfo::MAX_MIDI_EDITOR_ROW_HEIGHT;

    // Zoom state (vertical — horizontal is in base)
    int noteHeight_ = DEFAULT_NOTE_HEIGHT;

    // Chord row visibility
    bool showChordRow_ = false;
    bool isSyncingChords_ = false;  // Re-entry guard for syncChordAnnotations

    // Initial centering flag
    bool needsInitialCentering_ = true;

    // Components (PianoRoll-specific)
    std::unique_ptr<magda::PianoRollGridComponent> gridComponent_;
    std::unique_ptr<magda::PianoRollKeyboard> keyboard_;
    std::unique_ptr<VerticalZoomStrip> verticalZoomStrip_;
    std::unique_ptr<magda::SvgButton> chordToggle_;
    std::unique_ptr<magda::SvgButton> chordDetectBtn_;
    std::unique_ptr<magda::SvgButton> velocityToggle_;
    std::unique_ptr<magda::SvgButton> pitchGlideToggle_;
    std::unique_ptr<magda::SvgButton> ccLanesBtn_;

    // CC strip button is lit while the drawer is open with CC/pitchbend lanes
    void updateCcLanesButtonState();

    // Live MIDI note monitor hooks (plumbing lives in MidiEditorContent).
    void highlightMonitoredNote(int noteNumber, bool noteOn) override;
    void ensureMonitoredNoteVisible(int noteNumber) override;

    // Grid component management
    void setupGridCallbacks();
    void drawSidebar(juce::Graphics& g, juce::Rectangle<int> area);
    void drawChordRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawVelocityHeader(juce::Graphics& g, juce::Rectangle<int> area);
    void detectChordsFromNotes();
    void syncChordAnnotations(magda::ClipId clipId);
    void setNoteHeight(int height, bool persist);
    void setNoteHeightAnchored(int height, int anchorNote, int anchorScreenY, bool persist);
    void loadNoteHeightFromClip(magda::ClipId clipId);

    // Multi-track overlay: push the shared overlay set into the grid
    void applyOverlayTracks() override;

    // Helper to get current header height based on chord row visibility
    int getHeaderHeight() const {
        return showChordRow_ ? HEADER_HEIGHT : RULER_HEIGHT;
    }

    std::unique_ptr<magda::OctaveLabelStrip> octaveLabelStrip_;

    // Center the view on middle C (C4)
    void centerOnNote(int noteNumber);
    void centerOnNotes();
    // Scrolls vertically only if the note is off-screen, and only far enough to
    // bring it flush to the nearest edge (no re-centering, no horizontal move).
    void ensureNoteVisible(int noteNumber);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollContent)
};

}  // namespace magda::daw::ui
