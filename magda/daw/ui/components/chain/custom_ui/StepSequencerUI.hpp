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
 * row each for gate, tie, glide and accent. Too short for the keyboard, a step's box opens one in
 * a popup instead (#3205). The control column on the right holds the slots, the
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
    /// Tall enough for the keyboard at its full height.
    static int preferredHeight();

    /// The keyboard alone, a piano roll once lent: what expanding into the arrangement lends
    /// (#3205).
    juce::Component& expandablePart() {
        return keyboard_;
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
    daw::audio::StepSequencerPlugin* device_ = nullptr;
    step_pattern::MonoPattern pattern_;

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
    // Set from the step menu.
    int velocity_ = 90;
    int accentVelocity_ = 120;

    // --- State ---
    int selectedStep_ = 0;       // Currently selected step for editing
    int currentPlayStep_ = -1;   // Step being played (for highlight)
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

    static constexpr int MIN_KEYBOARD_HEIGHT = 48;
    static constexpr int MAX_KEYBOARD_HEIGHT = 84;

    /** The keys that set the selected step's note, with their octave buttons in the gutter. Lent
     *  to the arrangement, a piano roll of the whole pattern instead, one note per step (#3205). */
    class NoteEditor : public juce::Component {
      public:
        explicit NoteEditor(StepSequencerUI& owner) : owner_(owner) {}

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e,
                            const juce::MouseWheelDetails& wheel) override;

      private:
        static constexpr int OCTAVE_BUTTON_HEIGHT = 22;
        /// In the arrangement the keys stop growing here.
        static constexpr int MAX_KEYS_HEIGHT = 160;
        static constexpr int STRIP_WIDTH = 20;
        static constexpr int BUTTON_BOX_HEIGHT = 44;
        static constexpr int MIN_VISIBLE_NOTES = 12;
        static constexpr int MAX_VISIBLE_NOTES = 48;

        StepSequencerUI& owner_;
        int baseNote_ = 48;
        int lowNote_ = 48;
        int visibleNotes_ = 24;
        juce::Rectangle<int> labelArea_, downArea_, upArea_, keysArea_;
        juce::Rectangle<int> rollUpArea_, rollDownArea_, zoomInArea_, zoomOutArea_, pianoArea_,
            cellArea_;

        bool showsRoll() const;
        int octaves() const;
        void drawOctaveButtons(juce::Graphics& g);
        void paintRoll(juce::Graphics& g);
        void rollMouseDown(const juce::MouseEvent& e);
        void shiftWindow(int semitones);
        void zoomBy(int deltaNotes);
    };

    NoteEditor keyboard_{*this};

    bool keyboardLent() const;
    /// Repaints the faceplate and the keyboard, which may be lent to the arrangement.
    void redraw();

    void drawNoteRow(juce::Graphics& g);

    void toggleStepFlag(const juce::MouseEvent& e);
    void setStepNote(int stepIndex, int note);
    void openNotePicker(int stepIndex);

    // --- Layout bounds (computed in resized, used in paint/mouseDown) ---
    juce::Rectangle<int> rulerArea_;
    juce::Rectangle<int> noteArea_;
    juce::Rectangle<int> gateArea_;
    juce::Rectangle<int> tieArea_;
    juce::Rectangle<int> glideArea_;
    juce::Rectangle<int> accentArea_;
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
