#pragma once

#include <memory>

#include "PanelContent.hpp"
#include "core/ClipManager.hpp"
#include "ui/state/TimelineController.hpp"

namespace magda {
class TimeRuler;
}

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
class DrumGridClipContent : public PanelContent,
                            public magda::ClipManagerListener,
                            public magda::TimelineStateListener {
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

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;

    // TimelineStateListener
    void timelineStateChanged(const magda::TimelineState& state,
                              magda::ChangeFlags changes) override;

    // Set the clip to edit
    void setClip(magda::ClipId clipId);
    magda::ClipId getEditingClipId() const {
        return editingClipId_;
    }

    // Row model (public so grid/label components can access)
    struct PadRow {
        int noteNumber = 0;
        juce::String name;
        bool hasChain = false;
    };

  private:
    magda::ClipId editingClipId_ = magda::INVALID_CLIP_ID;
    daw::audio::DrumGridPlugin* drumGrid_ = nullptr;

    // Layout constants
    static constexpr int LABEL_WIDTH = 120;
    static constexpr int RULER_HEIGHT = 36;
    static constexpr int ROW_HEIGHT = 24;
    static constexpr int GRID_LEFT_PADDING = 2;

    // Zoom limits
    static constexpr double MIN_HORIZONTAL_ZOOM = 10.0;
    static constexpr double MAX_HORIZONTAL_ZOOM = 500.0;

    // Zoom state
    double horizontalZoom_ = 50.0;  // pixels per beat

    // Drum grid note range
    int baseNote_ = 36;
    int numPads_ = 16;  // Show 16 pads by default

    std::vector<PadRow> padRows_;

    // Components
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<DrumGridClipGrid> gridComponent_;
    std::unique_ptr<DrumGridRowLabels> rowLabels_;
    std::unique_ptr<magda::TimeRuler> timeRuler_;

    void buildPadRows();
    void updateGridSize();
    void updateTimeRuler();
    void findDrumGrid();
    juce::String resolvePadName(int padIndex) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridClipContent)
};

}  // namespace magda::daw::ui
