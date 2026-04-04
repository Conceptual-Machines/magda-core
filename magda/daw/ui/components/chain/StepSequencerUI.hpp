#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/StepSequencerPlugin.hpp"
#include "core/Config.hpp"
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
 */
class StepSequencerUI : public juce::Component,
                        private juce::ValueTree::Listener,
                        private juce::Timer,
                        private magda::ConfigListener {
  public:
    StepSequencerUI();
    ~StepSequencerUI() override;

    void setPlugin(daw::audio::StepSequencerPlugin* plugin);

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

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

    // --- MIDI controls ---
    std::unique_ptr<magda::SvgButton> midiThruButton_;
    std::unique_ptr<magda::SvgButton> stepRecordButton_;

    // --- Pattern generation ---
    std::unique_ptr<magda::SvgButton> randomButton_;
    juce::TextButton aiButton_{"AI"};
    juce::TextEditor aiPromptEditor_;
    juce::Label aiModelLabel_;
    std::unique_ptr<magda::SvgButton> aiIcon_;
    std::unique_ptr<magda::SvgButton> aiClearButton_;

    /** Shows streaming status: description + step list that auto-scrolls. */
    class AIResultDisplay : public juce::Component, private juce::Timer {
      public:
        AIResultDisplay();
        void paint(juce::Graphics& g) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        void setStreamingText(const juce::String& text);
        void appendStreamingToken(const juce::String& token);
        void showResult(const juce::String& description, int numSteps);
        void clear();

      private:
        void timerCallback() override;

        enum class Mode { Empty, Streaming };
        Mode mode_ = Mode::Empty;
        juce::String text_;
        juce::String description_;
        std::vector<audio::StepSequencerPlugin::Step> previewSteps_;
        int scrollOffset_ = 0;
        bool autoScroll_ = true;
        float spinnerAngle_ = 0.0f;
    };
    AIResultDisplay aiResultDisplay_;

    // --- State ---
    int selectedStep_ = 0;       // Currently selected step for editing
    int currentPlayStep_ = -1;   // Step being played (for highlight)
    int keyboardBaseNote_ = 48;  // Current keyboard base note (shifts with octave arrows)
    int dragSourceStep_ = -1;    // Source step for shift+drag copy
    int dragTargetStep_ = -1;    // Current drag target (for visual feedback)
    bool wasRecording_ = false;  // Previous recording state (for header repaint)

    // --- Layout constants ---
    static constexpr int CONTROL_ROW_HEIGHT = 22;
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
    void drawStepBoxes(juce::Graphics& g, juce::Rectangle<int> area);
    void drawAccentRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawGlideTieRow(juce::Graphics& g, juce::Rectangle<int> area);
    void drawKeyboard(juce::Graphics& g, juce::Rectangle<int> area);
    void drawOctaveArrow(juce::Graphics& g, juce::Rectangle<int> area, bool isLeft);

    // --- Hit testing ---
    int getStepAtX(int x, int areaX, int areaWidth, int numSteps) const;
    int getKeyboardNoteAtPosition(juce::Point<int> pos, juce::Rectangle<int> area) const;

    // --- Layout bounds (computed in resized, used in paint/mouseDown) ---
    juce::Rectangle<int> stepBoxArea_;
    juce::Rectangle<int> accentArea_;
    juce::Rectangle<int> glideTieArea_;
    juce::Rectangle<int> keyboardArea_;
    juce::Rectangle<int> octaveDownArea_;
    juce::Rectangle<int> octaveUpArea_;
    juce::Rectangle<int> rampArea_;
    juce::Rectangle<int> buttonArea_;
    int dividerX_ = 0, dividerY_ = 0, dividerHeight_ = 0;
    int streamSeparatorY_ = 0;

    // --- Setup helpers ---
    void setupLabel(juce::Label& label, const juce::String& text);
    void setupSlider(LinkableTextSlider& slider, double min, double max, double step);

    void syncFromPlugin();

    // ConfigListener
    void configChanged() override;

    // ValueTree::Listener
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

    // Timer — poll playback position
    void timerCallback() override;

    // AI pattern generation
    void generateAIPattern();

    // Context menu
    void showStepContextMenu(int stepIndex);

    // Note name helper
    static juce::String noteNameShort(int noteNumber);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerUI)
};

}  // namespace magda::daw::ui
