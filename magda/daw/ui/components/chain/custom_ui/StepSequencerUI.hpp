// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <vector>

#include "audio/plugins/StepSequencerPlugin.hpp"
#include "core/ParameterInfo.hpp"
#include "core/StepPatternCommands.hpp"
#include "custom_ui/SequencerFaceplate.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief 303-style step sequencer UI.
 *
 * The steps on the left: their notes over a keyboard that sets the selected step's note, then a
 * row each for gate, tie, glide and accent. The control column on the right holds the slots, the
 * pattern's length and the time bend.
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

    /// Fired when a non-slot setting (the time bend, quantize, subdivision) is
    /// edited, carrying ALL of them in the device's own state
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

    /// Shows or hides the time bend beside the controls.
    void setTimeBendShown(bool shown);
    /// The steps at their usual size beside the controls as they show now.
    int preferredWidth() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    daw::audio::StepSequencerPlugin* device_ = nullptr;
    step_pattern::MonoPattern pattern_;

    sequencer_faceplate::SequencerControlPanel panel_;
    LinkableTextSlider velocity_;
    LinkableTextSlider accentVelocity_;

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

    static constexpr int NOTE_ROW_HEIGHT = 28;
    static constexpr int OCTAVE_BUTTON_HEIGHT = 22;
    static constexpr int MIN_KEYBOARD_HEIGHT = 36;
    static constexpr int MAX_KEYBOARD_HEIGHT = 84;
    static constexpr int MIN_BASE_NOTE = 0;
    static constexpr int MAX_BASE_NOTE = 96;

    /// Octaves the keyboard shows at its width.
    int keyboardOctaves() const;
    int keyboardNoteCount() const {
        return keyboardOctaves() * 12;
    }

    void drawNoteRow(juce::Graphics& g);
    void drawKeyboard(juce::Graphics& g);
    void drawOctaveButtons(juce::Graphics& g);

    int getKeyboardNoteAtPosition(juce::Point<int> pos) const;
    void toggleStepFlag(const juce::MouseEvent& e);

    // --- Layout bounds (computed in resized, used in paint/mouseDown) ---
    juce::Rectangle<int> rulerArea_;
    juce::Rectangle<int> noteArea_;
    juce::Rectangle<int> keyboardArea_;
    juce::Rectangle<int> octaveLabelArea_;
    juce::Rectangle<int> octaveDownArea_;
    juce::Rectangle<int> octaveUpArea_;
    juce::Rectangle<int> gateArea_;
    juce::Rectangle<int> tieArea_;
    juce::Rectangle<int> glideArea_;
    juce::Rectangle<int> accentArea_;
    int dividerX_ = 0;

    void sendChange(int paramIndex, float value);
    void settingsEdited();
    /// Sends a bend edit on: AMOUNT and CYCLES as slots, the rest as settings.
    void timeBendEdited(const sdk::sequencer::TimeBend& before,
                        const sdk::sequencer::TimeBend& after);
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerUI)
};

}  // namespace magda::daw::ui
