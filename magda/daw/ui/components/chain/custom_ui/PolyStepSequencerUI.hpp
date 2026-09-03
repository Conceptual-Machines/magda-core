#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <vector>

#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "core/ChainNodePath.hpp"
#include "core/ParameterInfo.hpp"
#include "core/StepPatternCommands.hpp"
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
 * persisted in the device's state ("seqViewMode") so it restores with the
 * project.
 *
 * Written against the model (#2299/#2313): slot values arrive through
 * updateFromParameters() and leave through onParameterChanged, the pattern
 * arrives through setPattern() and every edit to it leaves through
 * onPatternEdited as one undoable command. The device pointer stays for what
 * only the running device knows - the play step and the step recorder - and the
 * device path for the drum-lane discovery, which walks the live chain.
 */
class PolyStepSequencerUI : public juce::Component, private juce::Timer {
  public:
    PolyStepSequencerUI();
    ~PolyStepSequencerUI() override;

    void setSequencer(daw::audio::PolyStepSequencerPlugin* device);
    void setDevicePath(const magda::ChainNodePath& devicePath);

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    /// The pattern the model holds. Redrawing follows; nothing is sent back.
    void setPattern(const step_pattern::PolyPattern& pattern);

    std::function<void(int paramIndex, float value)> onParameterChanged;

    /// Non-slot settings (quantize, subdivision, ramp cycles, hard angle, view
    /// mode) in the device's own state vocabulary, for the owner to patch onto
    /// the model's document (#2317).
    std::function<void(const juce::NamedValueSet&)> onSettingsEdited;

    /// Every pattern edit, for the owner to commit as one undoable step. A
    /// continuous gesture (a velocity or probability drag) says so, and the
    /// owner folds its run of edits into one entry.
    std::function<void(const juce::String& description,
                       std::function<void(step_pattern::PolyPattern&)>, magda::StepPatternGesture)>
        onPatternEdited;

    /// Which drag the continuous edits - the velocity and probability lanes,
    /// the step-count slider - currently belong to. Bumped when a drag ends, so
    /// consecutive gestures are separate undo steps rather than one merged run
    /// (#2335, and DrumGridUI::getFaderGesture for the same idea).
    int patternGesture() const {
        return patternGesture_;
    }

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void lookAndFeelChanged() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    /** What a pattern view draws from and edits through. */
    struct ViewContext {
        /// The model's pattern, owned by the UI and refreshed by setPattern().
        const step_pattern::PolyPattern* pattern = nullptr;
        /// The device, for the chain walk that finds the drum lanes.
        magda::ChainNodePath devicePath;
        /// One undoable pattern edit.
        std::function<void(const juce::String&, std::function<void(step_pattern::PolyPattern&)>,
                           magda::StepPatternGesture)>
            edit;
    };

    /** Base for pattern-view modes (keys / drum), swapped via the mode toggle. */
    class PatternView : public juce::Component {
      public:
        ~PatternView() override = default;

        virtual void setContext(const ViewContext& context);
        virtual void setPlayStep(int step) = 0;
        virtual void patternChanged() = 0;

      protected:
        /// The model's pattern, or an empty one before the view is bound.
        const step_pattern::PolyPattern& pattern() const;

        /// One undoable pattern edit, through the owner.
        void editPattern(
            const juce::String& description, std::function<void(step_pattern::PolyPattern&)> edit,
            magda::StepPatternGesture gesture = magda::StepPatternGesture::Discrete) const;

        /// The step / pattern menu both views show. False when @p result names
        /// none of its items, so the caller can leave the view alone.
        bool applyStepMenuAction(int result, int stepIndex);

        /// Called after a transpose the view asked for, so a pitch-windowed
        /// view can follow the notes it just moved.
        virtual void patternTransposed(int) {}

        ViewContext context_;
    };

    class KeysView;       // Piano-roll style pitch x step grid (defined in .cpp)
    class DrumLanesView;  // x0x drum-lane grid driven by a downstream Drum Grid (defined in .cpp)

    daw::audio::PolyStepSequencerPlugin* device_ = nullptr;
    magda::ChainNodePath devicePath_;
    step_pattern::PolyPattern pattern_;
    // Mirror of the curve display's hard-angle toggle, so settingsEdited() can
    // publish the full settings set from the controls alone.
    bool hardAngle_ = false;

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
    /// Which view is actually built, so a mode change swaps it exactly once.
    bool usingDrumView_ = false;

    // Right-side control panel bounds (controls + time bend), painted as a card.
    juce::Rectangle<int> sidePanelArea_;

    // --- Pattern view (swapped between KeysView and DrumLanesView) ---
    std::unique_ptr<PatternView> patternView_;

    // --- State ---
    int currentPlayStep_ = -1;  // Step being played (for highlight)

    // Lane being drag-edited (velocity / probability bars)
    enum class DragLane { None, Velocity, Probability };
    DragLane activeDragLane_ = DragLane::None;

    // Starts above kNoStepPatternGesture and never repeats within a session,
    // so a faceplate rebuilt over the same device cannot reuse a token the
    // command still on top of the undo stack is carrying.
    static std::atomic<int> nextPatternGesture_;
    int patternGesture_ = nextPatternGesture_.fetch_add(1);

    /// End the current continuous gesture, so the next one is its own undo.
    void endPatternGesture() {
        patternGesture_ = nextPatternGesture_.fetch_add(1);
    }

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

    void sendChange(int paramIndex, float value);
    void settingsEdited();
    void syncSettingsFromDevice();

    /// Run @p edit over the model's pattern as one undoable step.
    void editPattern(const juce::String& description,
                     std::function<void(step_pattern::PolyPattern&)> edit,
                     magda::StepPatternGesture gesture = magda::StepPatternGesture::Discrete);

    /// Notes the device's step recorder captured, committed to the model.
    void drainRecordedSteps();

    /// Hand the current pattern, path and edit route to the pattern view.
    void pushViewContext();

    /** Swap the pattern view to match the persisted view mode. */
    void applyPatternViewMode();

    // Timer — playhead animation only
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyStepSequencerUI)
};

}  // namespace magda::daw::ui
