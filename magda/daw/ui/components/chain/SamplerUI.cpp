#include "SamplerUI.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

SamplerUI::SamplerUI() {
    // Sample name label
    sampleNameLabel_.setText("No sample loaded", juce::dontSendNotification);
    sampleNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    sampleNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    sampleNameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(sampleNameLabel_);

    // Load button
    loadButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    loadButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    loadButton_.onClick = [this]() {
        if (onLoadSampleRequested)
            onLoadSampleRequested();
    };
    addAndMakeVisible(loadButton_);

    // --- ADSR sliders ---
    auto setupTimeSlider = [this](TextSlider& slider, int paramIndex, double min, double max,
                                  double defaultVal) {
        slider.setRange(min, max, 0.001);
        slider.setValue(defaultVal, juce::dontSendNotification);
        slider.setValueFormatter([](double v) {
            if (v < 0.01)
                return juce::String(v * 1000.0, 1) + " ms";
            if (v < 1.0)
                return juce::String(v * 1000.0, 0) + " ms";
            return juce::String(v, 2) + " s";
        });
        slider.setValueParser([](const juce::String& text) {
            juce::String t = text.trim();
            if (t.endsWithIgnoreCase("ms"))
                return static_cast<double>(t.dropLastCharacters(2).trim().getFloatValue()) / 1000.0;
            if (t.endsWithIgnoreCase("s"))
                return static_cast<double>(t.dropLastCharacters(1).trim().getFloatValue());
            double v = t.getDoubleValue();
            return v > 10.0 ? v / 1000.0 : v;  // assume ms if > 10
        });
        slider.onValueChanged = [this, paramIndex](double value) {
            if (onParameterChanged)
                onParameterChanged(paramIndex, static_cast<float>(value));
        };
        addAndMakeVisible(slider);
    };

    setupTimeSlider(attackSlider_, 0, 0.001, 5.0, 0.001);
    setupTimeSlider(decaySlider_, 1, 0.001, 5.0, 0.1);

    // Sustain (0-1, no units)
    sustainSlider_.setRange(0.0, 1.0, 0.01);
    sustainSlider_.setValue(1.0, juce::dontSendNotification);
    sustainSlider_.setValueFormatter(
        [](double v) { return juce::String(static_cast<int>(v * 100)) + "%"; });
    sustainSlider_.setValueParser([](const juce::String& text) {
        juce::String t = text.trim();
        if (t.endsWithIgnoreCase("%"))
            t = t.dropLastCharacters(1).trim();
        double v = t.getDoubleValue();
        return v > 1.0 ? v / 100.0 : v;
    });
    sustainSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(value));
    };
    addAndMakeVisible(sustainSlider_);

    setupTimeSlider(releaseSlider_, 3, 0.001, 10.0, 0.1);

    // --- Pitch slider (-24 to +24 semitones) ---
    pitchSlider_.setRange(-24.0, 24.0, 1.0);
    pitchSlider_.setValue(0.0, juce::dontSendNotification);
    pitchSlider_.setValueFormatter(
        [](double v) { return juce::String(static_cast<int>(v)) + " st"; });
    pitchSlider_.setValueParser([](const juce::String& text) {
        return static_cast<double>(
            text.trim().upToFirstOccurrenceOf(" ", false, false).getIntValue());
    });
    pitchSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(4, static_cast<float>(value));
    };
    addAndMakeVisible(pitchSlider_);

    // --- Fine slider (-100 to +100 cents) ---
    fineSlider_.setRange(-100.0, 100.0, 1.0);
    fineSlider_.setValue(0.0, juce::dontSendNotification);
    fineSlider_.setValueFormatter(
        [](double v) { return juce::String(static_cast<int>(v)) + " ct"; });
    fineSlider_.setValueParser([](const juce::String& text) {
        return static_cast<double>(
            text.trim().upToFirstOccurrenceOf(" ", false, false).getIntValue());
    });
    fineSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(5, static_cast<float>(value));
    };
    addAndMakeVisible(fineSlider_);

    // --- Level slider (-60 to +12 dB) ---
    levelSlider_.setRange(-60.0, 12.0, 0.1);
    levelSlider_.setValue(0.0, juce::dontSendNotification);
    levelSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(6, static_cast<float>(value));
    };
    addAndMakeVisible(levelSlider_);

    // --- Labels ---
    setupLabel(attackLabel_, "ATK");
    setupLabel(decayLabel_, "DEC");
    setupLabel(sustainLabel_, "SUS");
    setupLabel(releaseLabel_, "REL");
    setupLabel(pitchLabel_, "PITCH");
    setupLabel(fineLabel_, "FINE");
    setupLabel(levelLabel_, "LEVEL");
}

void SamplerUI::setupLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void SamplerUI::updateParameters(float attack, float decay, float sustain, float release,
                                 float pitch, float fine, float level,
                                 const juce::String& sampleName) {
    attackSlider_.setValue(attack, juce::dontSendNotification);
    decaySlider_.setValue(decay, juce::dontSendNotification);
    sustainSlider_.setValue(sustain, juce::dontSendNotification);
    releaseSlider_.setValue(release, juce::dontSendNotification);
    pitchSlider_.setValue(pitch, juce::dontSendNotification);
    fineSlider_.setValue(fine, juce::dontSendNotification);
    levelSlider_.setValue(level, juce::dontSendNotification);

    if (sampleName.isNotEmpty()) {
        sampleNameLabel_.setText(sampleName, juce::dontSendNotification);
        sampleNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    } else {
        sampleNameLabel_.setText("No sample loaded", juce::dontSendNotification);
        sampleNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    }
}

void SamplerUI::setWaveformData(const juce::AudioBuffer<float>* buffer, double /*sampleRate*/) {
    if (buffer == nullptr || buffer->getNumSamples() == 0) {
        hasWaveform_ = false;
        waveformPath_.clear();
        repaint();
        return;
    }

    hasWaveform_ = true;
    // We'll rebuild the path in paint() since we need the bounds
    buildWaveformPath(buffer, getWidth() > 0 ? getWidth() - 16 : 200, 40);
    repaint();
}

void SamplerUI::buildWaveformPath(const juce::AudioBuffer<float>* buffer, int width, int height) {
    waveformPath_.clear();
    if (buffer == nullptr || width <= 0 || height <= 0)
        return;

    const float* data = buffer->getReadPointer(0);
    int numSamples = buffer->getNumSamples();
    float samplesPerPixel = static_cast<float>(numSamples) / static_cast<float>(width);
    float halfHeight = static_cast<float>(height) * 0.5f;

    waveformPath_.startNewSubPath(0.0f, halfHeight);

    for (int x = 0; x < width; ++x) {
        int startSample = static_cast<int>(x * samplesPerPixel);
        int endSample = juce::jmin(static_cast<int>((x + 1) * samplesPerPixel), numSamples);

        float maxVal = 0.0f;
        for (int s = startSample; s < endSample; ++s) {
            float absVal = std::abs(data[s]);
            if (absVal > maxVal)
                maxVal = absVal;
        }

        float y = halfHeight - maxVal * halfHeight;
        waveformPath_.lineTo(static_cast<float>(x), y);
    }

    // Mirror for bottom half
    for (int x = width - 1; x >= 0; --x) {
        int startSample = static_cast<int>(x * samplesPerPixel);
        int endSample = juce::jmin(static_cast<int>((x + 1) * samplesPerPixel), numSamples);

        float maxVal = 0.0f;
        for (int s = startSample; s < endSample; ++s) {
            float absVal = std::abs(data[s]);
            if (absVal > maxVal)
                maxVal = absVal;
        }

        float y = halfHeight + maxVal * halfHeight;
        waveformPath_.lineTo(static_cast<float>(x), y);
    }

    waveformPath_.closeSubPath();
}

void SamplerUI::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));

    // Waveform area
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(26);  // Skip sample name row

    auto waveformArea = area.removeFromTop(44);
    if (hasWaveform_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
        auto pathBounds = waveformArea.reduced(0, 2).toFloat();
        g.saveState();
        g.addTransform(juce::AffineTransform::translation(pathBounds.getX(), pathBounds.getY()));
        g.fillPath(waveformPath_);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.7f));
        g.strokePath(waveformPath_, juce::PathStrokeType(0.5f));
        g.restoreState();
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRect(waveformArea);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("Drop sample or click Load", waveformArea, juce::Justification::centred);
    }
}

void SamplerUI::resized() {
    auto area = getLocalBounds().reduced(8);

    // Row 1: Sample name + Load button
    auto sampleRow = area.removeFromTop(22);
    loadButton_.setBounds(sampleRow.removeFromRight(50));
    sampleRow.removeFromRight(4);
    sampleNameLabel_.setBounds(sampleRow);
    area.removeFromTop(4);

    // Row 2: Waveform display (painted, not a component)
    area.removeFromTop(44);
    area.removeFromTop(4);

    // Row 3: ADSR labels
    auto adsrLabelRow = area.removeFromTop(12);
    int colWidth = area.getWidth() / 4;
    attackLabel_.setBounds(adsrLabelRow.removeFromLeft(colWidth));
    decayLabel_.setBounds(adsrLabelRow.removeFromLeft(colWidth));
    sustainLabel_.setBounds(adsrLabelRow.removeFromLeft(colWidth));
    releaseLabel_.setBounds(adsrLabelRow);

    // Row 4: ADSR sliders
    auto adsrRow = area.removeFromTop(22);
    attackSlider_.setBounds(adsrRow.removeFromLeft(colWidth).reduced(2, 0));
    decaySlider_.setBounds(adsrRow.removeFromLeft(colWidth).reduced(2, 0));
    sustainSlider_.setBounds(adsrRow.removeFromLeft(colWidth).reduced(2, 0));
    releaseSlider_.setBounds(adsrRow.reduced(2, 0));
    area.removeFromTop(4);

    // Row 5: Pitch / Fine / Level labels
    auto pitchLabelRow = area.removeFromTop(12);
    int thirdWidth = area.getWidth() / 3;
    pitchLabel_.setBounds(pitchLabelRow.removeFromLeft(thirdWidth));
    fineLabel_.setBounds(pitchLabelRow.removeFromLeft(thirdWidth));
    levelLabel_.setBounds(pitchLabelRow);

    // Row 6: Pitch / Fine / Level sliders
    auto pitchRow = area.removeFromTop(22);
    pitchSlider_.setBounds(pitchRow.removeFromLeft(thirdWidth).reduced(2, 0));
    fineSlider_.setBounds(pitchRow.removeFromLeft(thirdWidth).reduced(2, 0));
    levelSlider_.setBounds(pitchRow.reduced(2, 0));
}

}  // namespace magda::daw::ui
