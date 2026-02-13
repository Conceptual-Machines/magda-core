#pragma once

#include <memory>

#include "MidiEditorContent.hpp"

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
class DrumGridClipContent : public MidiEditorContent {
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
        return LABEL_WIDTH;
    }
    void updateGridSize() override;
    void setGridPixelsPerBeat(double ppb) override;
    void setGridPlayheadPosition(double position) override;
    void onScrollPositionChanged(int scrollX, int scrollY) override;

    daw::audio::DrumGridPlugin* drumGrid_ = nullptr;

    // Layout constants (DrumGrid-specific)
    static constexpr int LABEL_WIDTH = 120;
    static constexpr int ROW_HEIGHT = 24;

    // Drum grid note range
    int baseNote_ = 36;
    int numPads_ = 16;

    std::vector<PadRow> padRows_;

    // Components (DrumGrid-specific)
    std::unique_ptr<DrumGridClipGrid> gridComponent_;
    std::unique_ptr<DrumGridRowLabels> rowLabels_;

    void buildPadRows();
    void findDrumGrid();
    juce::String resolvePadName(int padIndex) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridClipContent)
};

}  // namespace magda::daw::ui
