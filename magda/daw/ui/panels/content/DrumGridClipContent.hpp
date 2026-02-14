#pragma once

#include <memory>

#include "MidiEditorContent.hpp"

namespace magda {
class SvgButton;
class VelocityLaneComponent;
}  // namespace magda

namespace magda::daw::audio {
class DrumGridPlugin;
class MagdaSamplerPlugin;
}  // namespace magda::daw::audio

namespace magda::daw::ui {

class DrumGridClipGrid;
class DrumGridRowLabels;

/**
 * @brief Drum grid clip editor for MIDI clips on DrumGrid tracks
 *
 * Displays MIDI notes as a drum-machine-style grid:
 * - Pad names on the left (resolved from sample names or MIDI note names)
 * - Hit grid on the right (rows = pads, columns = beat subdivisions)
 * - Time ruler along the top
 * - Click cells to add/remove MIDI notes (toggle)
 */
class DrumGridClipContent : public MidiEditorContent, private juce::Timer {
  public:
    DrumGridClipContent();
    ~DrumGridClipContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::DrumGridClipView;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::DrumGridClipView, "Drum Grid", "Drum grid MIDI editor",
                "DrumGrid"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void onActivated() override;
    void onDeactivated() override;

    // ClipManagerListener overrides
    void clipsChanged() override;
    void clipSelectionChanged(magda::ClipId clipId) override;

    // Set the clip to edit
    void setClip(magda::ClipId clipId);

    // Row model (public so grid/label components can access)
    struct PadRow {
        int noteNumber = 0;
        juce::String name;
        bool hasChain = false;
    };

  private:
    // MidiEditorContent virtual implementations
    int getLeftPanelWidth() const override {
        return SIDEBAR_WIDTH + LABEL_WIDTH;
    }
    void updateGridSize() override;
    void setGridPixelsPerBeat(double ppb) override;
    void setGridPlayheadPosition(double position) override;
    void onScrollPositionChanged(int scrollX, int scrollY) override;
    void onGridResolutionChanged() override;

    daw::audio::DrumGridPlugin* drumGrid_ = nullptr;

    // Layout constants (DrumGrid-specific)
    static constexpr int SIDEBAR_WIDTH = 32;
    static constexpr int LABEL_WIDTH = 120;
    static constexpr int ROW_HEIGHT = 24;
    static constexpr int VELOCITY_LANE_HEIGHT = 80;
    static constexpr int VELOCITY_HEADER_HEIGHT = 20;

    // Drum grid note range
    int baseNote_ = 0;
    int numPads_ = 128;

    std::vector<PadRow> padRows_;

    // Velocity drawer visibility
    bool velocityDrawerOpen_ = false;

    // Components (DrumGrid-specific)
    std::unique_ptr<DrumGridClipGrid> gridComponent_;
    std::unique_ptr<DrumGridRowLabels> rowLabels_;
    std::unique_ptr<magda::SvgButton> controlsToggle_;
    std::unique_ptr<magda::VelocityLaneComponent> velocityLane_;

    void buildPadRows();
    void refreshPadRowNames();
    void findDrumGrid();
    void drawSidebar(juce::Graphics& g, juce::Rectangle<int> area);
    void drawVelocityHeader(juce::Graphics& g, juce::Rectangle<int> area);
    void updateVelocityLane();
    juce::String resolvePadName(int padIndex) const;
    void timerCallback() override;

    int getDrawerHeight() const {
        return velocityDrawerOpen_ ? (VELOCITY_LANE_HEIGHT + VELOCITY_HEADER_HEIGHT) : 0;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridClipContent)
};

}  // namespace magda::daw::ui
