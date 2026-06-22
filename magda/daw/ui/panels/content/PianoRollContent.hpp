#pragma once

#include <functional>
#include <memory>

#include "MidiEditorContent.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/pianoroll/PitchFoldMap.hpp"

namespace magda {
class PianoRollGridComponent;
class PianoRollKeyboard;
class OctaveLabelStrip;
class SvgButton;
class MidiBridge;
struct MidiNoteEvent;
}  // namespace magda

namespace magda::daw::ui {

class MidiTakeLanesComponent;

/**
 * @brief Piano roll editor for MIDI clips
 *
 * Displays MIDI notes in a piano roll grid layout:
 * - Keyboard on the left showing note names
 * - Note rectangles in the grid representing MIDI notes (interactive)
 * - Time ruler along the top (switchable between absolute/relative)
 */
class PianoRollContent : public MidiEditorContent,
                         public magda::SelectionManagerListener,
                         public juce::ChangeListener {
  public:
    PianoRollContent();
    ~PianoRollContent() override;

    // Notation (C / Do) changes repaint the chord lane immediately.
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        repaint();
    }

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
    void mouseDown(const juce::MouseEvent& e) override;

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

  protected:
    // Extension points for a chord-focused subclass (ChordClipContent). The
    // defaults preserve standard piano-roll behaviour, so PianoRollContent has
    // no chord-track knowledge of its own.
    //   chordRowHeight() - height of the chord lane when visible.
    //   chordFocusMode() - when true the chord lane is forced visible and the
    //                      velocity/CC lanes + toggles are hidden.
    virtual int chordRowHeight() const {
        return CHORD_ROW_HEIGHT;
    }
    virtual bool chordFocusMode() const {
        return false;
    }
    // Width of the left icon strip. A chord-focus subclass returns 0 to drop the
    // strip entirely (its toggles - chord/fold/velocity/CC - are all irrelevant
    // there).
    virtual int sidebarWidth() const {
        return SIDEBAR_WIDTH;
    }
    // chordGroup of the selected chord-lane block (0 = none). The chord editor
    // overrides this so drawChordRow() renders a selection ring + edge resize
    // handles on it.
    virtual int selectedChordGroup() const {
        return 0;
    }
    // chordGroup of the chord-lane block currently being auditioned (0 = none);
    // drawChordRow() tints it to show it's playing.
    virtual int previewChordGroup() const {
        return 0;
    }
    // Chord-focus mode replaces the chord-row "rescan" button with a show/hide
    // grid toggle in the gutter; this fires when it's clicked.
    virtual void onGridToggleClicked() {}
    // Whether the note grid is currently visible (drives the toggle's active
    // highlight). The chord editor overrides this.
    virtual bool gridShown() const {
        return true;
    }
    void setGridToggleActive(bool on);
    // Pixel x of a chord-lane beat (clip-relative). Inverse of chordRowBeatForX;
    // used to hit-test and draw chord blocks.
    int chordRowXForBeat(double clipRelativeBeat) const;
    // Left pixel x where the chord lane / grid content begins (right of the
    // keyboard column).
    int chordLaneLeftX() const {
        return sidebarWidth() + ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH;
    }
    // Top y of the chord lane. The ruler sits at the very top, so the chord lane
    // (when visible) starts just below it; 0 when the chord row is hidden.
    int chordRowTop() const {
        return showChordRow_ ? RULER_HEIGHT : 0;
    }
    // Called when the chord lane is clicked at the given clip-relative beat.
    // Return true to consume the click (the standard piano roll returns false so
    // the event falls through to the base handler). ChordClipContent uses this
    // to add a chord at the clicked position.
    virtual bool onChordRowClicked(double clipRelativeBeat) {
        (void)clipRelativeBeat;
        return false;
    }
    // Re-run chord detection from the clip's notes (rebuilds the chord lane).
    void redetectChords();
    // Clip-relative beat under an x pixel on the chord lane (>= 0). Shared by the
    // click-to-add handler and chord-drop targets.
    double chordRowBeatForX(int x) const;

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

    // Pitch fold (#1464): collapse the vertical axis to used pitches. The map
    // (foldMap_), enabled flag, rebuild/apply orchestration now live in the base
    // MidiEditorContent so the drum grid shares them. The piano roll provides
    // the pitch source (multi-clip union) and the fold-aware repaints/centering.
    std::vector<int> collectUsedPitches() const override;
    void onFoldMapChanged() override;
    void recenterOnNotes() override;

    // Chord row visibility
    bool showChordRow_ = false;
    bool isSyncingChords_ = false;  // Re-entry guard for syncChordAnnotations

    // Progression overlay (#1504): ghost the chord-track progression behind a
    // normal track's chord lane for reference. Global toggle, shared across
    // editors so it stays on as you move between clips.
    static bool showProgressionOverlay_;

    // Initial centering flag
    bool needsInitialCentering_ = true;

    // Components (PianoRoll-specific)
    std::unique_ptr<magda::PianoRollGridComponent> gridComponent_;
    std::unique_ptr<magda::PianoRollKeyboard> keyboard_;
    std::unique_ptr<VerticalZoomStrip> verticalZoomStrip_;
    std::unique_ptr<magda::SvgButton> foldToggle_;
    std::unique_ptr<magda::SvgButton> takeLanesToggle_;
    std::unique_ptr<magda::SvgButton> chordToggle_;
    std::unique_ptr<magda::SvgButton> chordDetectBtn_;
    std::unique_ptr<magda::SvgButton> progressionOverlayToggle_;  // #1504 ghost overlay
    std::unique_ptr<magda::SvgButton> gridToggleBtn_;             // chord mode: show/hide grid
    std::unique_ptr<magda::SvgButton> velocityToggle_;
    std::unique_ptr<magda::SvgButton> pitchGlideToggle_;
    std::unique_ptr<magda::SvgButton> ccLanesBtn_;

    // CC strip button is lit while the drawer is open with CC/pitchbend lanes
    void updateLaneToggleStates() override;

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
        return showChordRow_ ? (chordRowHeight() + RULER_HEIGHT) : RULER_HEIGHT;
    }

    std::unique_ptr<magda::OctaveLabelStrip> octaveLabelStrip_;

    // Folded take-lanes strip below the grid (MIDI comping, #1466). Visible when
    // the clip has >=2 takes and clip->takesExpanded.
    std::unique_ptr<MidiTakeLanesComponent> takeLanes_;
    bool takeLanesVisible() const;
    void refreshTakeLanes();

    // Center the view on middle C (C4)
    void centerOnNote(int noteNumber);
    void centerOnNotes();
    // Scrolls vertically only if the note is off-screen, and only far enough to
    // bring it flush to the nearest edge (no re-centering, no horizontal move).
    void ensureNoteVisible(int noteNumber);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollContent)
};

}  // namespace magda::daw::ui
