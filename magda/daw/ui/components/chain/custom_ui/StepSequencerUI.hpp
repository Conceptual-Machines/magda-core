#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <vector>

#include "audio/plugins/StepSequencerPlugin.hpp"
#include "core/ParameterInfo.hpp"
#include "core/StepPatternCommands.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/RampCurveDisplay.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief 303-style step sequencer UI.
 *
 * Layout (top to bottom):
 *   - Controls row: Rate, Steps, Direction, Swing, Glide Time
 *   - Step boxes row: 16/32 clickable step cells showing note name
 *   - Accent row: toggle accent per step
 *   - Glide row: toggle glide per step
 *   - Mini keyboard: 2-octave keyboard with octave arrows on each side
 *
 * Click a step to select it, then click a keyboard key to assign pitch.
 * Use < > arrows to shift the keyboard range up/down by one octave.
 * Click accent/glide toggles directly.
 *
 * Written against the model (#2299/#2313): slot values arrive through
 * updateFromParameters() and leave through onParameterChanged, the pattern
 * arrives through setPattern() and every edit to it leaves through
 * onPatternEdited as one undoable command. The device pointer stays for what
 * only the running device knows - the play step and the step recorder.
 */
class StepSequencerUI : public juce::Component, private juce::Timer {
  public:
    StepSequencerUI();
    ~StepSequencerUI() override;

    void setSequencer(daw::audio::StepSequencerPlugin* device);

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    /// The pattern the model holds. Redrawing follows; nothing is sent back.
    void setPattern(const step_pattern::MonoPattern& pattern);

    std::function<void(int paramIndex, float value)> onParameterChanged;

    /// Fired when a non-slot setting (ramp cycles, quantize, subdivision, hard
    /// angle) is edited, carrying ALL of them in the device's own state
    /// vocabulary (StepSequencerPlugin::SettingIDs). The owner patches the
    /// model's document; the projection updates the live device (#2317).
    std::function<void(const juce::NamedValueSet&)> onSettingsEdited;

    /// Fired for every pattern edit. The owner runs the change over the model's
    /// current pattern and commits it as one undoable step (#2313).
    std::function<void(const juce::String& description,
                       std::function<void(step_pattern::MonoPattern&)>, magda::StepPatternGesture)>
        onPatternEdited;

    /// Which drag the continuous edits currently belong to. Bumped when a drag
    /// ends, so consecutive gestures are separate undo steps rather than one
    /// merged run (#2335, and DrumGridUI::getFaderGesture for the same idea).
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
    daw::audio::StepSequencerPlugin* device_ = nullptr;
    step_pattern::MonoPattern pattern_;
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
    juce::Label glideLabel_;
    LinkableTextSlider glideSlider_;

    // --- Ramp curve ---
    juce::Label rampLabel_;
    RampCurveDisplay rampCurveDisplay_;
    juce::Label depthLabel_;
    LinkableTextSlider depthSlider_;
    juce::Label skewLabel_;
    LinkableTextSlider skewSlider_;
    juce::Label cyclesLabel_;
    LinkableTextSlider cyclesSlider_;
    juce::Label quantizeLabel_;
    LinkableTextSlider quantizeSlider_;
    juce::Label quantizeSubLabel_;
    LinkableTextSlider quantizeSubSlider_;

    // --- State ---
    int selectedStep_ = 0;       // Currently selected step for editing
    int currentPlayStep_ = -1;   // Step being played (for highlight)
    int keyboardBaseNote_ = 48;  // Current keyboard base note (shifts with octave arrows)
    int dragSourceStep_ = -1;    // Source step for shift+drag copy
    int dragTargetStep_ = -1;    // Current drag target (for visual feedback)
    bool wasRecording_ = false;  // Previous recording state (for header repaint)

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
    static constexpr int TIMELINE_HEIGHT = 12;
    static constexpr int STEP_BOX_SIZE = 22;
    static constexpr int TOGGLE_ROW_HEIGHT = 16;
    static constexpr int KEYBOARD_HEIGHT = 48;
    static constexpr int OCTAVE_ARROW_WIDTH = 20;
    static constexpr int ROW_GAP = 3;
    static constexpr int PADDING = 4;
    static constexpr int LABEL_WIDTH = 44;

    // --- Keyboard layout ---
    // 2 octaves visible at a time, scrollable via octave arrows
    static constexpr int KEYBOARD_NUM_NOTES = 24;
    static constexpr int MIN_BASE_NOTE = 0;    // C-1
    static constexpr int MAX_BASE_NOTE = 108;  // C8 (108 + 24 would be > 127)

    // --- Drawing helpers ---
    void drawTimeline(juce::Graphics& g, juce::Rectangle<int> area);
    void drawStepBoxes(juce::Graphics& g, juce::Rectangle<int> area);
    void drawAccentRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawGlideTieRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawKeyboard(juce::Graphics& g, juce::Rectangle<int> area);
    void drawOctaveArrow(juce::Graphics& g, juce::Rectangle<int> area, bool isLeft);

    // --- Hit testing ---
    int getStepAtX(int x, int areaX, int areaWidth, int numSteps) const;
    int getKeyboardNoteAtPosition(juce::Point<int> pos, juce::Rectangle<int> area) const;

    // --- Layout bounds (computed in resized, used in paint/mouseDown) ---
    juce::Rectangle<int> timelineArea_;
    juce::Rectangle<int> stepBoxArea_;
    juce::Rectangle<int> accentArea_;
    juce::Rectangle<int> glideTieArea_;
    juce::Rectangle<int> keyboardArea_;
    juce::Rectangle<int> octaveDownArea_;
    juce::Rectangle<int> octaveUpArea_;
    juce::Rectangle<int> rampArea_;
    juce::Rectangle<int> buttonArea_;
    int dividerX_ = 0, dividerY_ = 0, dividerHeight_ = 0;

    // --- Setup helpers ---
    void setupLabel(juce::Label& label, const juce::String& text);
    void setupSlider(LinkableTextSlider& slider, double min, double max, double step);

    void sendChange(int paramIndex, float value);
    void settingsEdited();
    void syncSettingsFromDevice();

    /// Run @p edit over the model's pattern as one undoable step.
    void editPattern(const juce::String& description,
                     std::function<void(step_pattern::MonoPattern&)> edit,
                     magda::StepPatternGesture gesture = magda::StepPatternGesture::Discrete);

    /// Notes the device's step recorder captured, committed to the model.
    void drainRecordedSteps();

    // Timer — poll playback position
    void timerCallback() override;

    // Context menu
    void showStepContextMenu(int stepIndex);

    // Note name helper
    static juce::String noteNameShort(int noteNumber);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerUI)
};

}  // namespace magda::daw::ui
