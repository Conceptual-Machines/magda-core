#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/RampCurveDisplay.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Polyphonic step sequencer UI (keys / drum modes).
 *
 * Layout (top to bottom):
 *   - Controls row: Rate, Steps, Direction
 *   - Controls row: Swing, Gate Length, Quantize, Sub, view mode, MIDI thru
 *   - Pattern view: pitch x step grid (keys) or drum-lane grid (drum)
 *   - Gate row: toggle gate per step
 *   - Tie row: toggle tie per step
 *   - Velocity lane: draggable per-step velocity bars
 *   - Probability lane: draggable per-step probability bars
 *   - Time bend: ramp curve with depth/skew/cycles sliders
 *
 * Click a grid cell to toggle that note at that step. Use the arrows on the
 * left (or the mouse wheel over the grid) to shift the visible note window.
 *
 * The pattern-grid area is a swappable PatternView child: KeysView is a
 * piano-roll style grid, DrumLanesView is a classic x0x lane layout whose
 * lanes follow a Drum Grid found downstream in the same chain. The mode is
 * persisted on the plugin state ("seqViewMode") so it restores with the edit.
 */
class PolyStepSequencerUI : public juce::Component,
                            private juce::ValueTree::Listener,
                            private juce::Timer {
  public:
    PolyStepSequencerUI();
    ~PolyStepSequencerUI() override;

    void setPlugin(daw::audio::PolyStepSequencerPlugin* plugin);

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    /** Base for pattern-view modes (keys / drum), swapped via the mode toggle. */
    class PatternView : public juce::Component {
      public:
        ~PatternView() override = default;
        virtual void setPlugin(daw::audio::PolyStepSequencerPlugin* plugin) = 0;
        virtual void setPlayStep(int step) = 0;
        virtual void patternChanged() = 0;
    };

    class KeysView;       // Piano-roll style pitch x step grid (defined in .cpp)
    class DrumLanesView;  // x0x drum-lane grid driven by a downstream Drum Grid (defined in .cpp)

    daw::audio::PolyStepSequencerPlugin* plugin_ = nullptr;
    juce::ValueTree watchedState_;

    // --- Controls ---
    juce::Label rateLabel_;
    LinkableTextSlider rateSlider_;
    juce::Label stepsLabel_;
    LinkableTextSlider stepsSlider_;
    juce::Label dirLabel_;
    juce::ComboBox dirCombo_;
    juce::Label swingLabel_;
    LinkableTextSlider swingSlider_;
    juce::Label gateLengthLabel_;
    LinkableTextSlider gateLengthSlider_;
    juce::Label quantizeLabel_;
    LinkableTextSlider quantizeSlider_;
    juce::Label quantizeSubLabel_;
    LinkableTextSlider quantizeSubSlider_;

    // --- Ramp curve (time bend) ---
    juce::Label rampLabel_;
    RampCurveDisplay rampCurveDisplay_;
    juce::Label depthLabel_;
    LinkableTextSlider depthSlider_;
    juce::Label skewLabel_;
    LinkableTextSlider skewSlider_;
    juce::Label cyclesLabel_;
    LinkableTextSlider cyclesSlider_;

    // --- View mode toggle (keys / drum) ---
    juce::TextButton viewModeButton_;
    bool drumViewActive_ = false;

    // Right-side control panel bounds (controls + time bend), painted as a card.
    juce::Rectangle<int> sidePanelArea_;

    // --- Pattern view (swapped between KeysView and DrumLanesView) ---
    std::unique_ptr<PatternView> patternView_;

    // --- State ---
    int currentPlayStep_ = -1;  // Step being played (for highlight)

    // Lane being drag-edited (velocity / probability bars)
    enum class DragLane { None, Velocity, Probability };
    DragLane activeDragLane_ = DragLane::None;

    // --- Layout constants ---
    static constexpr int CONTROL_ROW_HEIGHT = 22;
    static constexpr int TOGGLE_ROW_HEIGHT = 16;
    static constexpr int LANE_HEIGHT = 28;
    static constexpr int ROW_GAP = 3;
    static constexpr int PADDING = 4;
    static constexpr int LABEL_WIDTH = 44;

    /** Left inset shared by the grid (arrows + piano gutter) and the per-step
     *  lanes below it, so step columns line up vertically. */
    static constexpr int LEFT_GUTTER_WIDTH = 36;

    // --- Drawing helpers ---
    void drawToggleRow(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                       bool isTieRow);
    void drawBarLane(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                     bool isProbability);

    // --- Hit testing ---
    int getStepAtX(int x, int areaX, int areaWidth, int numSteps) const;
    void applyLaneDrag(const juce::MouseEvent& e);

    // --- Layout bounds (computed in resized, used in paint/mouse handlers) ---
    juce::Rectangle<int> gateArea_;
    juce::Rectangle<int> tieArea_;
    juce::Rectangle<int> velocityArea_;
    juce::Rectangle<int> probabilityArea_;

    // --- Setup helpers ---
    void setupLabel(juce::Label& label, const juce::String& text);
    void setupSlider(LinkableTextSlider& slider, double min, double max, double step);

    void syncFromPlugin();

    /** Swap the pattern view to match the plugin's persisted view mode. */
    void updatePatternViewMode();

    // ValueTree::Listener — reflects external changes (undo/redo, preset load)
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree& child, int index) override;

    // Timer — playhead animation only
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyStepSequencerUI)
};

}  // namespace magda::daw::ui
