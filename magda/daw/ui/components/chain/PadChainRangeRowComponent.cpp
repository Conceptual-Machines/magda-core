#include "PadChainRangeRowComponent.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

PadChainRangeRowComponent::PadChainRangeRowComponent(int padIndex) : padIndex_(padIndex) {
    // Name label
    nameLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Helper to set up a note slider (0-127, displayed as note names)
    auto setupNoteSlider = [this](TextSlider& slider) {
        slider.setRange(0.0, 127.0, 1.0);
        slider.setValue(60.0, juce::dontSendNotification);
        slider.setValueFormatter([](double v) { return midiNoteToName(static_cast<int>(v)); });
        slider.setValueParser([](const juce::String& text) {
            // Try parsing as note name first
            int note = noteNameToMidi(text);
            if (note >= 0)
                return static_cast<double>(note);
            // Fall back to numeric
            return text.getDoubleValue();
        });
        addAndMakeVisible(slider);
    };

    setupNoteSlider(lowNoteSlider_);
    setupNoteSlider(highNoteSlider_);
    setupNoteSlider(rootNoteSlider_);

    lowNoteSlider_.onValueChanged = [this](double) { fireRangeChanged(); };
    highNoteSlider_.onValueChanged = [this](double) { fireRangeChanged(); };
    rootNoteSlider_.onValueChanged = [this](double) { fireRangeChanged(); };
}

PadChainRangeRowComponent::~PadChainRangeRowComponent() = default;

void PadChainRangeRowComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
    }
    g.fillRoundedRectangle(bounds.toFloat(), 2.0f);

    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    }
    g.drawRoundedRectangle(bounds.toFloat(), 2.0f, 1.0f);
}

void PadChainRangeRowComponent::resized() {
    auto bounds = getLocalBounds().reduced(3, 2);

    // Name label
    nameLabel_.setBounds(bounds.removeFromLeft(50));
    bounds.removeFromLeft(4);

    // Three equal-width sliders for the remaining space
    int remainingWidth = bounds.getWidth();
    int sliderWidth = (remainingWidth - 8) / 3;  // 4px gap between each

    lowNoteSlider_.setBounds(bounds.removeFromLeft(sliderWidth));
    bounds.removeFromLeft(4);

    highNoteSlider_.setBounds(bounds.removeFromLeft(sliderWidth));
    bounds.removeFromLeft(4);

    rootNoteSlider_.setBounds(bounds);
}

void PadChainRangeRowComponent::mouseUp(const juce::MouseEvent& e) {
    if (!contains(e.getPosition()))
        return;
    if (onClicked)
        onClicked(padIndex_);
}

void PadChainRangeRowComponent::updateFromChain(const juce::String& name, int lowNote, int highNote,
                                                int rootNote) {
    nameLabel_.setText(name, juce::dontSendNotification);
    lowNoteSlider_.setValue(lowNote, juce::dontSendNotification);
    highNoteSlider_.setValue(highNote, juce::dontSendNotification);
    rootNoteSlider_.setValue(rootNote, juce::dontSendNotification);
}

void PadChainRangeRowComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

juce::String PadChainRangeRowComponent::midiNoteToName(int note) {
    return juce::MidiMessage::getMidiNoteName(note, true, true, 3);
}

int PadChainRangeRowComponent::noteNameToMidi(const juce::String& name) {
    // Try common note name formats: C4, C#4, Db4, etc.
    static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "B"};
    static const char* flatNames[] = {"C",  "Db", "D",  "Eb", "E",  "F",
                                      "Gb", "G",  "Ab", "A",  "Bb", "B"};

    juce::String t = name.trim();
    if (t.isEmpty())
        return -1;

    // Extract note letter + accidental
    int noteIndex = -1;
    int charsParsed = 0;

    for (int i = 0; i < 12; ++i) {
        juce::String sharp(noteNames[i]);
        juce::String flat(flatNames[i]);

        if (t.startsWithIgnoreCase(sharp) && sharp.length() >= (flat.length())) {
            if (noteIndex < 0 || sharp.length() > charsParsed) {
                noteIndex = i;
                charsParsed = sharp.length();
            }
        }
        if (t.startsWithIgnoreCase(flat) && flat.length() > charsParsed) {
            noteIndex = i;
            charsParsed = flat.length();
        }
    }

    if (noteIndex < 0)
        return -1;

    // Parse octave from remaining string (middle C = C3 with octave offset 3)
    juce::String octaveStr = t.substring(charsParsed).trim();
    if (octaveStr.isEmpty())
        return -1;

    int octave = octaveStr.getIntValue();
    // JUCE uses middle C = C3 when middleCOctave=3
    // MIDI note = (octave + 2) * 12 + noteIndex  (for middleC octave 3: C3 = 60)
    int midiNote = (octave + 2) * 12 + noteIndex;

    if (midiNote < 0 || midiNote > 127)
        return -1;

    return midiNote;
}

void PadChainRangeRowComponent::fireRangeChanged() {
    if (onRangeChanged)
        onRangeChanged(padIndex_, static_cast<int>(lowNoteSlider_.getValue()),
                       static_cast<int>(highNoteSlider_.getValue()),
                       static_cast<int>(rootNoteSlider_.getValue()));
}

}  // namespace magda::daw::ui
