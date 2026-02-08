#include "DebugDialog.hpp"

#include "../../audio/MidiBridge.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "DebugSettings.hpp"

namespace magda::daw::ui {

std::unique_ptr<DebugDialog> DebugDialog::instance_;
magda::MidiBridge* DebugDialog::midiBridge_ = nullptr;

//==============================================================================
// Helper: format MIDI note name
//==============================================================================
static juce::String midiNoteToName(int noteNumber) {
    static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "B"};
    int octave = (noteNumber / 12) - 1;
    return juce::String(noteNames[noteNumber % 12]) + juce::String(octave);
}

//==============================================================================
// Content component with sliders and MIDI monitor
//==============================================================================
class DebugDialog::Content : public juce::Component, private juce::Timer {
  public:
    Content() {
        // Title
        titleLabel_.setText("Debug Settings", juce::dontSendNotification);
        titleLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
        titleLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        addAndMakeVisible(titleLabel_);

        // Bottom panel height
        bottomPanelLabel_.setText("Bottom Panel Height:", juce::dontSendNotification);
        bottomPanelLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        bottomPanelLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        addAndMakeVisible(bottomPanelLabel_);

        bottomPanelSlider_.setRange(100, 600, 1);
        bottomPanelSlider_.setValue(DebugSettings::getInstance().getBottomPanelHeight(),
                                    juce::dontSendNotification);
        bottomPanelSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        bottomPanelSlider_.onValueChange = [this]() {
            DebugSettings::getInstance().setBottomPanelHeight(
                static_cast<int>(bottomPanelSlider_.getValue()));
        };
        addAndMakeVisible(bottomPanelSlider_);

        // Device slot width
        deviceWidthLabel_.setText("Device Slot Width:", juce::dontSendNotification);
        deviceWidthLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        deviceWidthLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        addAndMakeVisible(deviceWidthLabel_);

        deviceWidthSlider_.setRange(100, 400, 1);
        deviceWidthSlider_.setValue(DebugSettings::getInstance().getDeviceSlotWidth(),
                                    juce::dontSendNotification);
        deviceWidthSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        deviceWidthSlider_.onValueChange = [this]() {
            DebugSettings::getInstance().setDeviceSlotWidth(
                static_cast<int>(deviceWidthSlider_.getValue()));
        };
        addAndMakeVisible(deviceWidthSlider_);

        // Button font size
        buttonFontLabel_.setText("Button Font Size:", juce::dontSendNotification);
        buttonFontLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        buttonFontLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        addAndMakeVisible(buttonFontLabel_);

        buttonFontSlider_.setRange(6, 16, 0.5);
        buttonFontSlider_.setValue(DebugSettings::getInstance().getButtonFontSize(),
                                   juce::dontSendNotification);
        buttonFontSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        buttonFontSlider_.onValueChange = [this]() {
            DebugSettings::getInstance().setButtonFontSize(
                static_cast<float>(buttonFontSlider_.getValue()));
        };
        addAndMakeVisible(buttonFontSlider_);

        // Param label font size
        paramFontLabel_.setText("Param Label Font Size:", juce::dontSendNotification);
        paramFontLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        paramFontLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        addAndMakeVisible(paramFontLabel_);

        paramFontSlider_.setRange(6, 14, 0.5);
        paramFontSlider_.setValue(DebugSettings::getInstance().getParamLabelFontSize(),
                                  juce::dontSendNotification);
        paramFontSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        paramFontSlider_.onValueChange = [this]() {
            DebugSettings::getInstance().setParamLabelFontSize(
                static_cast<float>(paramFontSlider_.getValue()));
        };
        addAndMakeVisible(paramFontSlider_);

        // Param value font size
        paramValueFontLabel_.setText("Param Value Font Size:", juce::dontSendNotification);
        paramValueFontLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        paramValueFontLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        addAndMakeVisible(paramValueFontLabel_);

        paramValueFontSlider_.setRange(6, 14, 0.5);
        paramValueFontSlider_.setValue(DebugSettings::getInstance().getParamValueFontSize(),
                                       juce::dontSendNotification);
        paramValueFontSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        paramValueFontSlider_.onValueChange = [this]() {
            DebugSettings::getInstance().setParamValueFontSize(
                static_cast<float>(paramValueFontSlider_.getValue()));
        };
        addAndMakeVisible(paramValueFontSlider_);

        // === MIDI Monitor Section ===
        midiMonitorLabel_.setText("MIDI Monitor", juce::dontSendNotification);
        midiMonitorLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
        midiMonitorLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        addAndMakeVisible(midiMonitorLabel_);

        midiLog_.setMultiLine(true);
        midiLog_.setReadOnly(true);
        midiLog_.setScrollbarsShown(true);
        midiLog_.setCaretVisible(false);
        midiLog_.setFont(
            juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        midiLog_.setColour(juce::TextEditor::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND));
        midiLog_.setColour(juce::TextEditor::textColourId,
                           DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        addAndMakeVisible(midiLog_);

        clearButton_.setButtonText("Clear");
        clearButton_.onClick = [this]() { midiLog_.clear(); };
        addAndMakeVisible(clearButton_);

        setSize(500, 500);

        // Start 30Hz timer for draining MIDI queue
        startTimerHz(30);
    }

    ~Content() override {
        stopTimer();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getPanelBackgroundColour());
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);

        titleLabel_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(10);

        // Bottom panel height row
        auto row = bounds.removeFromTop(24);
        bottomPanelLabel_.setBounds(row.removeFromLeft(140));
        bottomPanelSlider_.setBounds(row);
        bounds.removeFromTop(6);

        // Device width row
        row = bounds.removeFromTop(24);
        deviceWidthLabel_.setBounds(row.removeFromLeft(140));
        deviceWidthSlider_.setBounds(row);
        bounds.removeFromTop(6);

        // Button font size row
        row = bounds.removeFromTop(24);
        buttonFontLabel_.setBounds(row.removeFromLeft(140));
        buttonFontSlider_.setBounds(row);
        bounds.removeFromTop(6);

        // Param font size row
        row = bounds.removeFromTop(24);
        paramFontLabel_.setBounds(row.removeFromLeft(140));
        paramFontSlider_.setBounds(row);
        bounds.removeFromTop(6);

        // Param value font size row
        row = bounds.removeFromTop(24);
        paramValueFontLabel_.setBounds(row.removeFromLeft(140));
        paramValueFontSlider_.setBounds(row);
        bounds.removeFromTop(12);

        // MIDI Monitor section
        row = bounds.removeFromTop(24);
        midiMonitorLabel_.setBounds(row.removeFromLeft(200));
        clearButton_.setBounds(row.removeFromRight(60));
        bounds.removeFromTop(4);

        // MIDI log fills remaining space
        midiLog_.setBounds(bounds);
    }

  private:
    void timerCallback() override {
        if (!DebugDialog::midiBridge_)
            return;

        auto& queue = DebugDialog::midiBridge_->getGlobalEventQueue();
        magda::MidiEventEntry entry;
        bool appended = false;

        while (queue.pop(entry)) {
            auto line = formatMidiEvent(entry);
            midiLog_.moveCaretToEnd();
            midiLog_.insertTextAtCaret(line + "\n");
            appended = true;
            lineCount_++;
        }

        // Trim to ~200 lines
        if (appended && lineCount_ > 220) {
            auto text = midiLog_.getText();
            int linesToRemove = lineCount_ - 200;
            int pos = 0;
            for (int i = 0; i < linesToRemove && pos < text.length(); ++i) {
                pos = text.indexOf(pos, "\n") + 1;
                if (pos <= 0)
                    break;
            }
            if (pos > 0 && pos < text.length()) {
                midiLog_.setText(text.substring(pos));
                lineCount_ = 200;
            }
        }
    }

    static juce::String formatMidiEvent(const magda::MidiEventEntry& entry) {
        // Format timestamp as HH:MM:SS.mmm
        auto now = juce::Time::getCurrentTime();
        auto timeStr = now.formatted("[%H:%M:%S.") +
                       juce::String(now.getMilliseconds()).paddedLeft('0', 3) + "]";

        juce::String deviceStr = entry.deviceName.substring(0, 12).paddedRight(' ', 12);
        juce::String channelStr = "Ch." + juce::String(entry.channel).paddedLeft(' ', 2);

        switch (entry.type) {
            case magda::MidiEventEntry::NoteOn:
                return timeStr + " " + deviceStr + "  " + channelStr + "  NoteOn   " +
                       midiNoteToName(entry.data1).paddedRight(' ', 4) +
                       "vel=" + juce::String(entry.data2);
            case magda::MidiEventEntry::NoteOff:
                return timeStr + " " + deviceStr + "  " + channelStr + "  NoteOff  " +
                       midiNoteToName(entry.data1).paddedRight(' ', 4) +
                       "vel=" + juce::String(entry.data2);
            case magda::MidiEventEntry::CC:
                return timeStr + " " + deviceStr + "  " + channelStr + "  CC       " +
                       juce::String(entry.data1).paddedRight(' ', 4) +
                       "val=" + juce::String(entry.data2);
            case magda::MidiEventEntry::PitchBend: {
                int centered = entry.pitchBendValue - 8192;
                juce::String sign = centered >= 0 ? "+" : "";
                return timeStr + " " + deviceStr + "  " + channelStr + "  PitchBd  " + sign +
                       juce::String(centered);
            }
            default:
                return timeStr + " " + deviceStr + "  " + channelStr + "  Other";
        }
    }

    // Debug settings widgets
    juce::Label titleLabel_;
    juce::Label bottomPanelLabel_;
    juce::Slider bottomPanelSlider_;
    juce::Label deviceWidthLabel_;
    juce::Slider deviceWidthSlider_;
    juce::Label buttonFontLabel_;
    juce::Slider buttonFontSlider_;
    juce::Label paramFontLabel_;
    juce::Slider paramFontSlider_;
    juce::Label paramValueFontLabel_;
    juce::Slider paramValueFontSlider_;

    // MIDI Monitor widgets
    juce::Label midiMonitorLabel_;
    juce::TextEditor midiLog_;
    juce::TextButton clearButton_;
    int lineCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Content)
};

//==============================================================================
// DebugDialog
//==============================================================================
DebugDialog::DebugDialog()
    : DocumentWindow("Debug Settings", DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND),
                     DocumentWindow::closeButton) {
    content_ = std::make_unique<Content>();
    setContentNonOwned(content_.get(), true);
    setResizable(true, true);
    setUsingNativeTitleBar(true);
    centreWithSize(getWidth(), getHeight());
}

DebugDialog::~DebugDialog() = default;

void DebugDialog::closeButtonPressed() {
    hide();
}

void DebugDialog::show() {
    if (!instance_) {
        instance_ = std::make_unique<DebugDialog>();
    }
    instance_->setVisible(true);
    instance_->toFront(true);
}

void DebugDialog::hide() {
    if (instance_) {
        instance_->setVisible(false);
    }
}

void DebugDialog::setMidiBridge(magda::MidiBridge* bridge) {
    midiBridge_ = bridge;
}

}  // namespace magda::daw::ui
