#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/StepSequencerPlugin.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
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
 *   - Mini keyboard: 2-octave keyboard for pitch entry
 *   - Octave shift: -2 to +2 buttons
 *
 * Click a step to select it, then click a keyboard key to assign pitch.
 * Click accent/glide toggles directly.
 */
class StepSequencerUI : public juce::Component,
                        private juce::ValueTree::Listener,
                        private juce::Timer {
  public:
    StepSequencerUI();
    ~StepSequencerUI() override;

    void setPlugin(daw::audio::StepSequencerPlugin* plugin);

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    daw::audio::StepSequencerPlugin* plugin_ = nullptr;
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
    juce::Label glideLabel_;
    LinkableTextSlider glideSlider_;

    // --- State ---
    int selectedStep_ = 0;      // Currently selected step for editing
    int currentPlayStep_ = -1;  // Step being played (for highlight)

    // --- Layout constants ---
    static constexpr int CONTROL_ROW_HEIGHT = 22;
    static constexpr int STEP_BOX_SIZE = 22;
    static constexpr int TOGGLE_ROW_HEIGHT = 16;
    static constexpr int KEYBOARD_HEIGHT = 48;
    static constexpr int OCTAVE_ROW_HEIGHT = 20;
    static constexpr int ROW_GAP = 3;
    static constexpr int PADDING = 4;
    static constexpr int LABEL_WIDTH = 44;

    // --- Keyboard layout ---
    // 2 octaves: C3-B4 (notes 48-71)
    static constexpr int KEYBOARD_BASE_NOTE = 48;
    static constexpr int KEYBOARD_NUM_NOTES = 24;

    // --- Drawing helpers ---
    void drawStepBoxes(juce::Graphics& g, juce::Rectangle<int> area);
    void drawAccentRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawGlideRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawKeyboard(juce::Graphics& g, juce::Rectangle<int> area);
    void drawOctaveRow(juce::Graphics& g, juce::Rectangle<int> area);

    // --- Hit testing ---
    int getStepAtX(int x, int areaX, int areaWidth, int numSteps) const;
    int getKeyboardNoteAtPosition(juce::Point<int> pos, juce::Rectangle<int> area) const;
    int getOctaveShiftAtPosition(juce::Point<int> pos, juce::Rectangle<int> area) const;

    // --- Layout bounds (computed in resized, used in paint/mouseDown) ---
    juce::Rectangle<int> stepBoxArea_;
    juce::Rectangle<int> accentArea_;
    juce::Rectangle<int> glideArea_;
    juce::Rectangle<int> keyboardArea_;
    juce::Rectangle<int> octaveArea_;

    // --- Setup helpers ---
    void setupLabel(juce::Label& label, const juce::String& text);
    void setupSlider(LinkableTextSlider& slider, double min, double max, double step);

    void syncFromPlugin();

    // ValueTree::Listener
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

    // Timer — poll playback position
    void timerCallback() override;

    // Note name helper
    static juce::String noteNameShort(int noteNumber);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerUI)
};

}  // namespace magda::daw::ui
