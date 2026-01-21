#pragma once

#include <memory>

#include "PanelContent.hpp"
#include "core/ClipManager.hpp"

namespace magda {
class PianoRollGridComponent;
class PianoRollKeyboard;
class TimeRuler;
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
class PianoRollContent : public PanelContent, public magda::ClipManagerListener {
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

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;
    void clipDragPreview(magda::ClipId clipId, double previewStartTime,
                         double previewLength) override;

    // Set the clip to edit
    void setClip(magda::ClipId clipId);
    magda::ClipId getEditingClipId() const {
        return editingClipId_;
    }

    // Timeline mode
    void setRelativeTimeMode(bool relative);
    bool isRelativeTimeMode() const {
        return relativeTimeMode_;
    }

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
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;

    // Layout constants
    static constexpr int SIDEBAR_WIDTH = 32;
    static constexpr int KEYBOARD_WIDTH = 60;
    static constexpr int DEFAULT_NOTE_HEIGHT = 12;
    static constexpr int CHORD_ROW_HEIGHT = 24;                            // Chord detection row
    static constexpr int RULER_HEIGHT = 36;                                // Time ruler height
    static constexpr int HEADER_HEIGHT = CHORD_ROW_HEIGHT + RULER_HEIGHT;  // Total header
    static constexpr int MIN_NOTE = 21;                                    // A0
    static constexpr int MAX_NOTE = 108;                                   // C8
    static constexpr int GRID_LEFT_PADDING = 2;  // Small padding for timeline label visibility
    static constexpr int VELOCITY_LANE_HEIGHT = 80;
    static constexpr int VELOCITY_HEADER_HEIGHT = 20;

    // Zoom limits
    static constexpr double MIN_HORIZONTAL_ZOOM = 10.0;   // pixels per beat
    static constexpr double MAX_HORIZONTAL_ZOOM = 500.0;  // pixels per beat
    static constexpr int MIN_NOTE_HEIGHT = 6;
    static constexpr int MAX_NOTE_HEIGHT = 40;

    // Zoom state
    double horizontalZoom_ = 50.0;  // pixels per beat
    int noteHeight_ = DEFAULT_NOTE_HEIGHT;

    // Timeline mode (absolute vs relative)
    bool relativeTimeMode_ = true;  // Default to relative (1, 2, 3...)

    // Chord row visibility
    bool showChordRow_ = true;  // Default to visible

    // Velocity drawer visibility
    bool velocityDrawerOpen_ = false;  // Default to closed

    // Initial centering flag
    bool needsInitialCentering_ = true;

    // Components
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<magda::PianoRollGridComponent> gridComponent_;
    std::unique_ptr<magda::PianoRollKeyboard> keyboard_;
    std::unique_ptr<magda::TimeRuler> timeRuler_;
    std::unique_ptr<juce::TextButton> timeModeButton_;
    std::unique_ptr<juce::LookAndFeel> buttonLookAndFeel_;
    std::unique_ptr<magda::SvgButton> chordToggle_;
    std::unique_ptr<magda::SvgButton> velocityToggle_;
    std::unique_ptr<magda::VelocityLaneComponent> velocityLane_;

    // Grid component management
    void setupGridCallbacks();
    void updateGridSize();
    void updateTimeRuler();
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
