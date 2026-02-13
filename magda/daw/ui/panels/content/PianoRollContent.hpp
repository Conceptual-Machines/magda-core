#pragma once

#include <memory>

#include "MidiEditorContent.hpp"
#include "core/SelectionManager.hpp"

namespace magda {
class PianoRollGridComponent;
class PianoRollKeyboard;
class SvgButton;
class VelocityLaneComponent;
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
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void onActivated() override;
    void onDeactivated() override;

    // ClipManagerListener overrides
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;
    void clipDragPreview(magda::ClipId clipId, double previewStartTime,
                         double previewLength) override;

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void multiClipSelectionChanged(const std::unordered_set<magda::ClipId>& clipIds) override;

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

    // Velocity drawer visibility
    void setVelocityDrawerVisible(bool visible);
    bool isVelocityDrawerVisible() const {
        return velocityDrawerOpen_;
    }

  private:
    // MidiEditorContent virtual implementations
    int getLeftPanelWidth() const override {
        return SIDEBAR_WIDTH + KEYBOARD_WIDTH;
    }
    void updateGridSize() override;
    void updateTimeRuler() override;
    void setGridPixelsPerBeat(double ppb) override;
    void setGridPlayheadPosition(double position) override;
    void onScrollPositionChanged(int scrollX, int scrollY) override;

    // Layout constants (PianoRoll-specific)
    static constexpr int SIDEBAR_WIDTH = 32;
    static constexpr int KEYBOARD_WIDTH = 60;
    static constexpr int DEFAULT_NOTE_HEIGHT = 12;
    static constexpr int CHORD_ROW_HEIGHT = 24;
    static constexpr int HEADER_HEIGHT = CHORD_ROW_HEIGHT + RULER_HEIGHT;
    static constexpr int MIN_NOTE = 21;   // A0
    static constexpr int MAX_NOTE = 108;  // C8
    static constexpr int VELOCITY_LANE_HEIGHT = 80;
    static constexpr int VELOCITY_HEADER_HEIGHT = 20;

    // Vertical zoom limits
    static constexpr int MIN_NOTE_HEIGHT = 6;
    static constexpr int MAX_NOTE_HEIGHT = 40;

    // Zoom state (vertical — horizontal is in base)
    int noteHeight_ = DEFAULT_NOTE_HEIGHT;

    // Chord row visibility
    bool showChordRow_ = true;

    // Velocity drawer visibility
    bool velocityDrawerOpen_ = false;

    // Initial centering flag
    bool needsInitialCentering_ = true;

    // Components (PianoRoll-specific)
    std::unique_ptr<magda::PianoRollGridComponent> gridComponent_;
    std::unique_ptr<magda::PianoRollKeyboard> keyboard_;
    std::unique_ptr<magda::SvgButton> chordToggle_;
    std::unique_ptr<magda::SvgButton> velocityToggle_;
    std::unique_ptr<magda::VelocityLaneComponent> velocityLane_;

    // Grid component management
    void setupGridCallbacks();
    void drawSidebar(juce::Graphics& g, juce::Rectangle<int> area);
    void drawChordRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawVelocityHeader(juce::Graphics& g, juce::Rectangle<int> area);
    void updateVelocityLane();

    // Helper to get current header height based on chord row visibility
    int getHeaderHeight() const {
        return showChordRow_ ? HEADER_HEIGHT : RULER_HEIGHT;
    }

    // Helper to get current drawer height
    int getDrawerHeight() const {
        return velocityDrawerOpen_ ? (VELOCITY_LANE_HEIGHT + VELOCITY_HEADER_HEIGHT) : 0;
    }

    // Center the view on middle C (C4)
    void centerOnMiddleC();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollContent)
};

}  // namespace magda::daw::ui
