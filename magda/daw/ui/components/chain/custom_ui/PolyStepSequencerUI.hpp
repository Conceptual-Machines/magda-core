// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <vector>

#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "core/ChainNodePath.hpp"
#include "core/ParameterInfo.hpp"
#include "core/StepPatternCommands.hpp"
#include "custom_ui/SequencerFaceplate.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/SegmentedChoice.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Polyphonic step sequencer UI (keys / drum modes).
 *
 * The pattern on the left, a pitch x step grid or one lane per drum, then rows for gate, tie,
 * velocity and probability. Too short for the grid, a row of step boxes names each step's notes
 * and opens a picker for them (#3205). The control column on the right holds the slots, the
 * pattern's length, the view mode and the time bend.
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

    /// Shows or hides the time bend beside the controls.
    void setTimeBendShown(bool shown);
    /// The steps at their usual size beside the controls as they show now.
    int preferredWidth() const;
    /// Tall enough for the grid at a comfortable height.
    static int preferredHeight();

    /// The grid alone, what expanding into the arrangement lends out (#3205).
    juce::Component& expandablePart() {
        return patternHost_;
    }
    /// The part out in the arrangement, the steps or the time bend; null when neither is.
    juce::Component* lentPart() const;
    /// An expand button was clicked for @p part; only one part is out at a time.
    std::function<void(juce::Component& part)> onExpandClicked;
    /// A part is back from the arrangement.
    void expandablePartReturned();

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

    sequencer_faceplate::SettingsStrip strip_;
    TimeBendEditor timeBend_;
    std::unique_ptr<magda::SvgButton> bendExpandButton_ = sequencer_faceplate::makeExpandButton();
    bool timeBendShown_ = true;
    bool timeBendLent() const;
    /// Shown, and not out in the arrangement.
    bool timeBendInFaceplate() const;
    // Settings, not slots: edited from the right-click menu.
    double quantize_ = 0.0;
    int quantizeSub_ = 16;
    std::unique_ptr<magda::SvgButton> expandButton_ = sequencer_faceplate::makeExpandButton();
    magda::SegmentedChoice viewChoice_;
    bool drumViewActive_ = false;
    /// Which view is actually built, so a mode change swaps it exactly once.
    bool usingDrumView_ = false;

    /** Holds whichever pattern view shows, so the grid lent out survives a view swap. */
    class PatternHost : public juce::Component {
      public:
        void resized() override {
            for (auto* child : getChildren())
                child->setBounds(getLocalBounds());
        }
    };

    // --- Pattern view (swapped between KeysView and DrumLanesView) ---
    PatternHost patternHost_;
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

    static constexpr int MIN_LANE_HEIGHT = 18;
    static constexpr int MAX_LANE_HEIGHT = 26;
    /// Below this the grid's own scroll and zoom buttons do not fit, so the note row stands in.
    static constexpr int MIN_PATTERN_HEIGHT = 96;
    static constexpr int IDEAL_PATTERN_HEIGHT = 192;

    struct DrumLane {
        int note = 60;        // MIDI note that triggers this lane's sound
        juce::String label;   // Chain name / GM name / note name
        bool orphan = false;  // Pattern data with no matching Drum Grid chain
    };
    /// The drum lanes for the pattern on the device at @p devicePath.
    static std::vector<DrumLane> drumLanesFor(const magda::ChainNodePath& devicePath,
                                              const step_pattern::PolyPattern& pattern);

    /// What step @p index's box says: its note, its chord, its drum or how many it holds.
    juce::String noteCellText(int index, const std::vector<DrumLane>& lanes, const juce::Font& font,
                              float width) const;
    void openStepPicker(int stepIndex);
    void toggleNote(int stepIndex, int note);

    // --- Hit testing ---
    void applyLaneDrag(const juce::MouseEvent& e);

    // --- Layout bounds (computed in resized, used in paint/mouse handlers) ---
    juce::Rectangle<int> rulerArea_;
    juce::Rectangle<int> noteArea_;
    juce::Rectangle<int> gateArea_;
    juce::Rectangle<int> tieArea_;
    juce::Rectangle<int> velocityArea_;
    juce::Rectangle<int> probabilityArea_;
    int dividerX_ = -1;

    void sendChange(int paramIndex, float value);
    void settingsEdited();
    /// Sends a bend edit on: AMOUNT and CYCLES as slots, the rest as settings.
    /// Quantize and its grid, and for the mono sequencer the two velocities.
    void showSettingsMenu();
    void timeBendEdited(const sdk::sequencer::TimeBend& before,
                        const sdk::sequencer::TimeBend& after);
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
