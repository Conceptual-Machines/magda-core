#include "FourOscUI.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// =============================================================================
// FourOscUI
// =============================================================================

FourOscUI::FourOscUI() {
    tabs_ = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    tabs_->setTabBarDepth(20);

    auto tabBg = DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f);

    oscTab_ = std::make_unique<OscTab>(*this);
    filterTab_ = std::make_unique<FilterTab>(*this);
    ampTab_ = std::make_unique<AmpTab>(*this);
    modEnvTab_ = std::make_unique<ModEnvTab>(*this);
    lfoTab_ = std::make_unique<LFOTab>(*this);
    fxTab_ = std::make_unique<FXTab>(*this);

    tabs_->addTab("OSC", tabBg, oscTab_.get(), false);
    tabs_->addTab("Filter", tabBg, filterTab_.get(), false);
    tabs_->addTab("Amp", tabBg, ampTab_.get(), false);
    tabs_->addTab("Mod Env", tabBg, modEnvTab_.get(), false);
    tabs_->addTab("LFO", tabBg, lfoTab_.get(), false);
    tabs_->addTab("FX", tabBg, fxTab_.get(), false);

    addAndMakeVisible(*tabs_);
}

void FourOscUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    oscTab_->updateFromParameters(params);
    filterTab_->updateFromParameters(params);
    ampTab_->updateFromParameters(params);
    modEnvTab_->updateFromParameters(params);
    lfoTab_->updateFromParameters(params);
    fxTab_->updateFromParameters(params);
}

void FourOscUI::updatePluginState(const FourOscPluginState& state) {
    oscTab_->updatePluginState(state);
    filterTab_->updatePluginState(state);
    ampTab_->updatePluginState(state);
    lfoTab_->updatePluginState(state);
    fxTab_->updatePluginState(state);
}

void FourOscUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void FourOscUI::resized() {
    tabs_->setBounds(getLocalBounds());
}

// =============================================================================
// Helper: setup a small label
// =============================================================================

static void setupLabelStatic(juce::Label& label, const juce::String& text,
                             juce::Component* parent) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centred);
    parent->addAndMakeVisible(label);
}

// =============================================================================
// OscTab
// =============================================================================

FourOscUI::OscTab::OscTab(FourOscUI& owner) : owner_(owner) {
    setupLabel(hdrWave_, "WAVE");
    setupLabel(hdrTune_, "TUNE");
    setupLabel(hdrFine_, "FINE");
    setupLabel(hdrLevel_, "LEVEL");
    setupLabel(hdrPW_, "PW");
    setupLabel(hdrDetune_, "DET");
    setupLabel(hdrSpread_, "SPRD");
    setupLabel(hdrPan_, "PAN");
    setupLabel(hdrVoices_, "VOICES");

    for (int i = 0; i < 4; ++i) {
        auto& row = rows_[i];

        // Row label
        setupLabel(row.label, "OSC " + juce::String(i + 1));

        // Waveform combo
        row.waveformCombo.addItem("Sine", 1);
        row.waveformCombo.addItem("Triangle", 2);
        row.waveformCombo.addItem("Saw Up", 3);
        row.waveformCombo.addItem("Saw Down", 4);
        row.waveformCombo.addItem("Square", 5);
        row.waveformCombo.addItem("Noise", 6);
        row.waveformCombo.setSelectedId(1, juce::dontSendNotification);
        row.waveformCombo.onChange = [this, i]() {
            int shape = rows_[i].waveformCombo.getSelectedId() - 1;
            if (owner_.onPluginStateChanged)
                owner_.onPluginStateChanged("waveShape" + juce::String(i + 1), juce::var(shape));
        };
        addAndMakeVisible(row.waveformCombo);

        // Tune (-36 to 36 semitones)
        int oscBase = kOscBase + i * kOscParamsPerOsc;
        row.tuneSlider.setRange(-36.0, 36.0, 1.0);
        row.tuneSlider.setValue(0.0, juce::dontSendNotification);
        row.tuneSlider.onValueChanged = [this, idx = oscBase](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.tuneSlider);

        // Fine tune (-100 to 100 cents)
        row.fineSlider.setRange(-100.0, 100.0, 0.1);
        row.fineSlider.setValue(0.0, juce::dontSendNotification);
        row.fineSlider.onValueChanged = [this, idx = oscBase + 1](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.fineSlider);

        // Level (-100 to 0 dB)
        row.levelSlider.setRange(-100.0, 0.0, 0.1);
        row.levelSlider.setValue(-100.0, juce::dontSendNotification);
        row.levelSlider.onValueChanged = [this, idx = oscBase + 2](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.levelSlider);

        // Pulse width (0.01 to 0.99)
        row.pulseWidthSlider.setRange(0.01, 0.99, 0.01);
        row.pulseWidthSlider.setValue(0.5, juce::dontSendNotification);
        row.pulseWidthSlider.onValueChanged = [this, idx = oscBase + 3](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.pulseWidthSlider);

        // Detune (0 to 0.5)
        row.detuneSlider.setRange(0.0, 0.5, 0.001);
        row.detuneSlider.setValue(0.0, juce::dontSendNotification);
        row.detuneSlider.onValueChanged = [this, idx = oscBase + 4](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.detuneSlider);

        // Spread (-100 to 100 %)
        row.spreadSlider.setRange(-100.0, 100.0, 0.1);
        row.spreadSlider.setValue(0.0, juce::dontSendNotification);
        row.spreadSlider.onValueChanged = [this, idx = oscBase + 5](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.spreadSlider);

        // Pan (-1 to 1)
        row.panSlider.setRange(-1.0, 1.0, 0.01);
        row.panSlider.setValue(0.0, juce::dontSendNotification);
        row.panSlider.onValueChanged = [this, idx = oscBase + 6](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.panSlider);

        // Voices combo
        for (int v = 1; v <= 8; ++v)
            row.voicesCombo.addItem(juce::String(v), v);
        row.voicesCombo.setSelectedId(1, juce::dontSendNotification);
        row.voicesCombo.onChange = [this, i]() {
            int voices = rows_[i].voicesCombo.getSelectedId();
            if (owner_.onPluginStateChanged)
                owner_.onPluginStateChanged("voices" + juce::String(i + 1), juce::var(voices));
        };
        addAndMakeVisible(row.voicesCombo);
    }
}

void FourOscUI::OscTab::resized() {
    auto area = getLocalBounds().reduced(4);
    constexpr int rowH = 22;
    constexpr int labelW = 36;
    constexpr int comboW = 54;
    constexpr int sliderW = 44;
    constexpr int gap = 2;

    // Header row
    auto headerRow = area.removeFromTop(14);
    headerRow.removeFromLeft(labelW + gap);  // skip row label column
    hdrWave_.setBounds(headerRow.removeFromLeft(comboW));
    headerRow.removeFromLeft(gap);
    hdrTune_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrFine_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrLevel_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrPW_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrDetune_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrSpread_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrPan_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrVoices_.setBounds(headerRow.removeFromLeft(comboW));

    area.removeFromTop(2);

    for (int i = 0; i < 4; ++i) {
        auto rowArea = area.removeFromTop(rowH);
        area.removeFromTop(gap);

        auto& row = rows_[i];
        row.label.setBounds(rowArea.removeFromLeft(labelW));
        rowArea.removeFromLeft(gap);
        row.waveformCombo.setBounds(rowArea.removeFromLeft(comboW));
        rowArea.removeFromLeft(gap);
        row.tuneSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.fineSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.levelSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.pulseWidthSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.detuneSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.spreadSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.panSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.voicesCombo.setBounds(rowArea.removeFromLeft(comboW));
    }
}

void FourOscUI::OscTab::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (int i = 0; i < 4; ++i) {
        int base = kOscBase + i * kOscParamsPerOsc;
        if (base + 6 >= static_cast<int>(params.size()))
            break;
        auto& row = rows_[i];
        row.tuneSlider.setValue(params[static_cast<size_t>(base)].currentValue,
                                juce::dontSendNotification);
        row.fineSlider.setValue(params[static_cast<size_t>(base + 1)].currentValue,
                                juce::dontSendNotification);
        row.levelSlider.setValue(params[static_cast<size_t>(base + 2)].currentValue,
                                 juce::dontSendNotification);
        row.pulseWidthSlider.setValue(params[static_cast<size_t>(base + 3)].currentValue,
                                      juce::dontSendNotification);
        row.detuneSlider.setValue(params[static_cast<size_t>(base + 4)].currentValue,
                                  juce::dontSendNotification);
        row.spreadSlider.setValue(params[static_cast<size_t>(base + 5)].currentValue,
                                  juce::dontSendNotification);
        row.panSlider.setValue(params[static_cast<size_t>(base + 6)].currentValue,
                               juce::dontSendNotification);
    }
}

void FourOscUI::OscTab::updatePluginState(const FourOscPluginState& state) {
    for (int i = 0; i < 4; ++i) {
        rows_[i].waveformCombo.setSelectedId(state.oscWaveShape[i] + 1, juce::dontSendNotification);
        rows_[i].voicesCombo.setSelectedId(juce::jlimit(1, 8, state.oscVoices[i]),
                                           juce::dontSendNotification);
    }
}

void FourOscUI::OscTab::setupLabel(juce::Label& label, const juce::String& text) {
    setupLabelStatic(label, text, this);
}

// =============================================================================
// FilterTab
// =============================================================================

FourOscUI::FilterTab::FilterTab(FourOscUI& owner) : owner_(owner) {
    // Type combo
    setupLabel(typeLabel_, "TYPE");
    typeCombo_.addItem("Low Pass", 1);
    typeCombo_.addItem("Band Pass", 2);
    typeCombo_.addItem("High Pass", 3);
    typeCombo_.addItem("Notch", 4);
    typeCombo_.setSelectedId(1, juce::dontSendNotification);
    typeCombo_.onChange = [this]() {
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("filterType", juce::var(typeCombo_.getSelectedId() - 1));
    };
    addAndMakeVisible(typeCombo_);

    // Slope combo
    setupLabel(slopeLabel_, "SLOPE");
    slopeCombo_.addItem("12 dB", 1);
    slopeCombo_.addItem("24 dB", 2);
    slopeCombo_.setSelectedId(1, juce::dontSendNotification);
    slopeCombo_.onChange = [this]() {
        int slope = slopeCombo_.getSelectedId() == 1 ? 12 : 24;
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("filterSlope", juce::var(slope));
    };
    addAndMakeVisible(slopeCombo_);

    // Freq (0 to 135.076232 — MIDI note mapping)
    setupLabel(freqLabel_, "FREQ");
    freqSlider_.setRange(0.0, 135.076232, 0.01);
    freqSlider_.setValue(69.0, juce::dontSendNotification);
    freqSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 4, static_cast<float>(v));
    };
    addAndMakeVisible(freqSlider_);

    // Resonance (0-100 %)
    setupLabel(resLabel_, "RES");
    resonanceSlider_.setRange(0.0, 100.0, 0.1);
    resonanceSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 5, static_cast<float>(v));
    };
    addAndMakeVisible(resonanceSlider_);

    // Key tracking (0-100 %)
    setupLabel(keyLabel_, "KEY");
    keySlider_.setRange(0.0, 100.0, 0.1);
    keySlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 7, static_cast<float>(v));
    };
    addAndMakeVisible(keySlider_);

    // Velocity (0-100 %)
    setupLabel(velLabel_, "VEL");
    velocitySlider_.setRange(0.0, 100.0, 0.1);
    velocitySlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 8, static_cast<float>(v));
    };
    addAndMakeVisible(velocitySlider_);

    // Env amount (-1 to 1)
    setupLabel(amountLabel_, "AMT");
    amountSlider_.setRange(-1.0, 1.0, 0.01);
    amountSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 6, static_cast<float>(v));
    };
    addAndMakeVisible(amountSlider_);

    // Filter ADSR
    setupLabel(atkLabel_, "ATK");
    attackSlider_.setRange(0.0, 60.0, 0.001);
    attackSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase, static_cast<float>(v));
    };
    addAndMakeVisible(attackSlider_);

    setupLabel(decLabel_, "DEC");
    decaySlider_.setRange(0.0, 60.0, 0.001);
    decaySlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 1, static_cast<float>(v));
    };
    addAndMakeVisible(decaySlider_);

    setupLabel(susLabel_, "SUS");
    sustainSlider_.setRange(0.0, 100.0, 0.1);
    sustainSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 2, static_cast<float>(v));
    };
    addAndMakeVisible(sustainSlider_);

    setupLabel(relLabel_, "REL");
    releaseSlider_.setRange(0.0, 60.0, 0.001);
    releaseSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 3, static_cast<float>(v));
    };
    addAndMakeVisible(releaseSlider_);
}

void FourOscUI::FilterTab::resized() {
    auto area = getLocalBounds().reduced(4);
    constexpr int rowH = 22;
    constexpr int labelW = 36;
    constexpr int comboW = 80;
    constexpr int sliderW = 50;
    constexpr int gap = 4;

    // Row 1: Type, Slope combos
    auto row1 = area.removeFromTop(rowH);
    typeLabel_.setBounds(row1.removeFromLeft(labelW));
    row1.removeFromLeft(gap);
    typeCombo_.setBounds(row1.removeFromLeft(comboW));
    row1.removeFromLeft(gap + 8);
    slopeLabel_.setBounds(row1.removeFromLeft(labelW));
    row1.removeFromLeft(gap);
    slopeCombo_.setBounds(row1.removeFromLeft(comboW));
    area.removeFromTop(gap);

    // Row 2: Freq, Resonance, Key, Velocity, Amount
    auto row2 = area.removeFromTop(rowH);
    freqLabel_.setBounds(row2.removeFromLeft(labelW));
    row2.removeFromLeft(gap);
    freqSlider_.setBounds(row2.removeFromLeft(sliderW));
    row2.removeFromLeft(gap);
    resLabel_.setBounds(row2.removeFromLeft(labelW));
    row2.removeFromLeft(gap);
    resonanceSlider_.setBounds(row2.removeFromLeft(sliderW));
    row2.removeFromLeft(gap);
    keyLabel_.setBounds(row2.removeFromLeft(labelW));
    row2.removeFromLeft(gap);
    keySlider_.setBounds(row2.removeFromLeft(sliderW));
    area.removeFromTop(gap);

    // Row 3: Velocity, Amount
    auto row3 = area.removeFromTop(rowH);
    velLabel_.setBounds(row3.removeFromLeft(labelW));
    row3.removeFromLeft(gap);
    velocitySlider_.setBounds(row3.removeFromLeft(sliderW));
    row3.removeFromLeft(gap);
    amountLabel_.setBounds(row3.removeFromLeft(labelW));
    row3.removeFromLeft(gap);
    amountSlider_.setBounds(row3.removeFromLeft(sliderW));
    area.removeFromTop(gap);

    // Row 4: Filter ADSR
    auto row4 = area.removeFromTop(rowH);
    atkLabel_.setBounds(row4.removeFromLeft(labelW));
    row4.removeFromLeft(gap);
    attackSlider_.setBounds(row4.removeFromLeft(sliderW));
    row4.removeFromLeft(gap);
    decLabel_.setBounds(row4.removeFromLeft(labelW));
    row4.removeFromLeft(gap);
    decaySlider_.setBounds(row4.removeFromLeft(sliderW));
    row4.removeFromLeft(gap);
    susLabel_.setBounds(row4.removeFromLeft(labelW));
    row4.removeFromLeft(gap);
    sustainSlider_.setBounds(row4.removeFromLeft(sliderW));
    row4.removeFromLeft(gap);
    relLabel_.setBounds(row4.removeFromLeft(labelW));
    row4.removeFromLeft(gap);
    releaseSlider_.setBounds(row4.removeFromLeft(sliderW));
}

void FourOscUI::FilterTab::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (kFilterBase + 8 >= static_cast<int>(params.size()))
        return;
    attackSlider_.setValue(params[kFilterBase].currentValue, juce::dontSendNotification);
    decaySlider_.setValue(params[kFilterBase + 1].currentValue, juce::dontSendNotification);
    sustainSlider_.setValue(params[kFilterBase + 2].currentValue, juce::dontSendNotification);
    releaseSlider_.setValue(params[kFilterBase + 3].currentValue, juce::dontSendNotification);
    freqSlider_.setValue(params[kFilterBase + 4].currentValue, juce::dontSendNotification);
    resonanceSlider_.setValue(params[kFilterBase + 5].currentValue, juce::dontSendNotification);
    amountSlider_.setValue(params[kFilterBase + 6].currentValue, juce::dontSendNotification);
    keySlider_.setValue(params[kFilterBase + 7].currentValue, juce::dontSendNotification);
    velocitySlider_.setValue(params[kFilterBase + 8].currentValue, juce::dontSendNotification);
}

void FourOscUI::FilterTab::updatePluginState(const FourOscPluginState& state) {
    typeCombo_.setSelectedId(state.filterType + 1, juce::dontSendNotification);
    slopeCombo_.setSelectedId(state.filterSlope == 24 ? 2 : 1, juce::dontSendNotification);
}

void FourOscUI::FilterTab::setupLabel(juce::Label& label, const juce::String& text) {
    setupLabelStatic(label, text, this);
}

// =============================================================================
// AmpTab
// =============================================================================

FourOscUI::AmpTab::AmpTab(FourOscUI& owner) : owner_(owner) {
    setupLabel(atkLabel_, "ATK");
    attackSlider_.setRange(0.001, 60.0, 0.001);
    attackSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kAmpBase, static_cast<float>(v));
    };
    addAndMakeVisible(attackSlider_);

    setupLabel(decLabel_, "DEC");
    decaySlider_.setRange(0.001, 60.0, 0.001);
    decaySlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kAmpBase + 1, static_cast<float>(v));
    };
    addAndMakeVisible(decaySlider_);

    setupLabel(susLabel_, "SUS");
    sustainSlider_.setRange(0.0, 100.0, 0.1);
    sustainSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kAmpBase + 2, static_cast<float>(v));
    };
    addAndMakeVisible(sustainSlider_);

    setupLabel(relLabel_, "REL");
    releaseSlider_.setRange(0.001, 60.0, 0.001);
    releaseSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kAmpBase + 3, static_cast<float>(v));
    };
    addAndMakeVisible(releaseSlider_);

    setupLabel(velLabel_, "VEL");
    velocitySlider_.setRange(0.0, 100.0, 0.1);
    velocitySlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kAmpBase + 4, static_cast<float>(v));
    };
    addAndMakeVisible(velocitySlider_);

    analogButton_.onClick = [this]() {
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("ampAnalog", juce::var(analogButton_.getToggleState()));
    };
    addAndMakeVisible(analogButton_);
}

void FourOscUI::AmpTab::resized() {
    auto area = getLocalBounds().reduced(4);
    constexpr int rowH = 22;
    constexpr int labelW = 36;
    constexpr int sliderW = 50;
    constexpr int gap = 4;

    // Row 1: ADSR
    auto row1 = area.removeFromTop(rowH);
    atkLabel_.setBounds(row1.removeFromLeft(labelW));
    row1.removeFromLeft(gap);
    attackSlider_.setBounds(row1.removeFromLeft(sliderW));
    row1.removeFromLeft(gap);
    decLabel_.setBounds(row1.removeFromLeft(labelW));
    row1.removeFromLeft(gap);
    decaySlider_.setBounds(row1.removeFromLeft(sliderW));
    row1.removeFromLeft(gap);
    susLabel_.setBounds(row1.removeFromLeft(labelW));
    row1.removeFromLeft(gap);
    sustainSlider_.setBounds(row1.removeFromLeft(sliderW));
    row1.removeFromLeft(gap);
    relLabel_.setBounds(row1.removeFromLeft(labelW));
    row1.removeFromLeft(gap);
    releaseSlider_.setBounds(row1.removeFromLeft(sliderW));
    area.removeFromTop(gap);

    // Row 2: Velocity, Analog
    auto row2 = area.removeFromTop(rowH);
    velLabel_.setBounds(row2.removeFromLeft(labelW));
    row2.removeFromLeft(gap);
    velocitySlider_.setBounds(row2.removeFromLeft(sliderW));
    row2.removeFromLeft(gap + 8);
    analogButton_.setBounds(row2.removeFromLeft(80));
}

void FourOscUI::AmpTab::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (kAmpBase + 4 >= static_cast<int>(params.size()))
        return;
    attackSlider_.setValue(params[kAmpBase].currentValue, juce::dontSendNotification);
    decaySlider_.setValue(params[kAmpBase + 1].currentValue, juce::dontSendNotification);
    sustainSlider_.setValue(params[kAmpBase + 2].currentValue, juce::dontSendNotification);
    releaseSlider_.setValue(params[kAmpBase + 3].currentValue, juce::dontSendNotification);
    velocitySlider_.setValue(params[kAmpBase + 4].currentValue, juce::dontSendNotification);
}

void FourOscUI::AmpTab::updatePluginState(const FourOscPluginState& state) {
    analogButton_.setToggleState(state.ampAnalog, juce::dontSendNotification);
}

void FourOscUI::AmpTab::setupLabel(juce::Label& label, const juce::String& text) {
    setupLabelStatic(label, text, this);
}

// =============================================================================
// ModEnvTab
// =============================================================================

FourOscUI::ModEnvTab::ModEnvTab(FourOscUI& owner) : owner_(owner) {
    setupLabel(hdrAtk_, "ATK");
    setupLabel(hdrDec_, "DEC");
    setupLabel(hdrSus_, "SUS");
    setupLabel(hdrRel_, "REL");

    for (int i = 0; i < 2; ++i) {
        auto& row = rows_[i];
        int base = kModEnvBase + i * kModEnvParamsPerEnv;

        setupLabel(row.label, "ENV " + juce::String(i + 1));

        row.attackSlider.setRange(0.0, 60.0, 0.001);
        row.attackSlider.onValueChanged = [this, idx = base](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.attackSlider);

        row.decaySlider.setRange(0.0, 60.0, 0.001);
        row.decaySlider.onValueChanged = [this, idx = base + 1](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.decaySlider);

        row.sustainSlider.setRange(0.0, 100.0, 0.1);
        row.sustainSlider.onValueChanged = [this, idx = base + 2](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.sustainSlider);

        row.releaseSlider.setRange(0.001, 60.0, 0.001);
        row.releaseSlider.onValueChanged = [this, idx = base + 3](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.releaseSlider);
    }
}

void FourOscUI::ModEnvTab::resized() {
    auto area = getLocalBounds().reduced(4);
    constexpr int rowH = 22;
    constexpr int labelW = 36;
    constexpr int sliderW = 50;
    constexpr int gap = 4;

    // Header
    auto headerRow = area.removeFromTop(14);
    headerRow.removeFromLeft(labelW + gap);
    hdrAtk_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrDec_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrSus_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrRel_.setBounds(headerRow.removeFromLeft(sliderW));
    area.removeFromTop(2);

    for (int i = 0; i < 2; ++i) {
        auto rowArea = area.removeFromTop(rowH);
        area.removeFromTop(gap);
        auto& row = rows_[i];
        row.label.setBounds(rowArea.removeFromLeft(labelW));
        rowArea.removeFromLeft(gap);
        row.attackSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.decaySlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.sustainSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.releaseSlider.setBounds(rowArea.removeFromLeft(sliderW));
    }
}

void FourOscUI::ModEnvTab::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (int i = 0; i < 2; ++i) {
        int base = kModEnvBase + i * kModEnvParamsPerEnv;
        if (base + 3 >= static_cast<int>(params.size()))
            break;
        rows_[i].attackSlider.setValue(params[static_cast<size_t>(base)].currentValue,
                                       juce::dontSendNotification);
        rows_[i].decaySlider.setValue(params[static_cast<size_t>(base + 1)].currentValue,
                                      juce::dontSendNotification);
        rows_[i].sustainSlider.setValue(params[static_cast<size_t>(base + 2)].currentValue,
                                        juce::dontSendNotification);
        rows_[i].releaseSlider.setValue(params[static_cast<size_t>(base + 3)].currentValue,
                                        juce::dontSendNotification);
    }
}

void FourOscUI::ModEnvTab::setupLabel(juce::Label& label, const juce::String& text) {
    setupLabelStatic(label, text, this);
}

// =============================================================================
// LFOTab
// =============================================================================

FourOscUI::LFOTab::LFOTab(FourOscUI& owner) : owner_(owner) {
    setupLabel(hdrWave_, "WAVE");
    setupLabel(hdrRate_, "RATE");
    setupLabel(hdrDepth_, "DEPTH");
    setupLabel(hdrSync_, "SYNC");

    for (int i = 0; i < 2; ++i) {
        auto& row = rows_[i];
        int base = kLfoBase + i * kLfoParamsPerLfo;

        setupLabel(row.label, "LFO " + juce::String(i + 1));

        // Wave combo
        row.waveCombo.addItem("Sine", 1);
        row.waveCombo.addItem("Triangle", 2);
        row.waveCombo.addItem("Saw Up", 3);
        row.waveCombo.addItem("Saw Down", 4);
        row.waveCombo.addItem("Square", 5);
        row.waveCombo.addItem("Random", 6);
        row.waveCombo.setSelectedId(1, juce::dontSendNotification);
        row.waveCombo.onChange = [this, i]() {
            int shape = rows_[i].waveCombo.getSelectedId() - 1;
            if (owner_.onPluginStateChanged)
                owner_.onPluginStateChanged("lfoShape" + juce::String(i + 1), juce::var(shape));
        };
        addAndMakeVisible(row.waveCombo);

        // Rate (0-500 Hz)
        row.rateSlider.setRange(0.0, 500.0, 0.01);
        row.rateSlider.onValueChanged = [this, idx = base](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.rateSlider);

        // Depth (0-1)
        row.depthSlider.setRange(0.0, 1.0, 0.001);
        row.depthSlider.onValueChanged = [this, idx = base + 1](double v) {
            if (owner_.onParameterChanged)
                owner_.onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(row.depthSlider);

        // Sync toggle
        row.syncButton.onClick = [this, i]() {
            if (owner_.onPluginStateChanged)
                owner_.onPluginStateChanged("lfoSync" + juce::String(i + 1),
                                            juce::var(rows_[i].syncButton.getToggleState()));
        };
        addAndMakeVisible(row.syncButton);
    }
}

void FourOscUI::LFOTab::resized() {
    auto area = getLocalBounds().reduced(4);
    constexpr int rowH = 22;
    constexpr int labelW = 36;
    constexpr int comboW = 70;
    constexpr int sliderW = 50;
    constexpr int toggleW = 50;
    constexpr int gap = 4;

    // Header
    auto headerRow = area.removeFromTop(14);
    headerRow.removeFromLeft(labelW + gap);
    hdrWave_.setBounds(headerRow.removeFromLeft(comboW));
    headerRow.removeFromLeft(gap);
    hdrRate_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrDepth_.setBounds(headerRow.removeFromLeft(sliderW));
    headerRow.removeFromLeft(gap);
    hdrSync_.setBounds(headerRow.removeFromLeft(toggleW));
    area.removeFromTop(2);

    for (int i = 0; i < 2; ++i) {
        auto rowArea = area.removeFromTop(rowH);
        area.removeFromTop(gap);
        auto& row = rows_[i];
        row.label.setBounds(rowArea.removeFromLeft(labelW));
        rowArea.removeFromLeft(gap);
        row.waveCombo.setBounds(rowArea.removeFromLeft(comboW));
        rowArea.removeFromLeft(gap);
        row.rateSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.depthSlider.setBounds(rowArea.removeFromLeft(sliderW));
        rowArea.removeFromLeft(gap);
        row.syncButton.setBounds(rowArea.removeFromLeft(toggleW));
    }
}

void FourOscUI::LFOTab::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    for (int i = 0; i < 2; ++i) {
        int base = kLfoBase + i * kLfoParamsPerLfo;
        if (base + 1 >= static_cast<int>(params.size()))
            break;
        rows_[i].rateSlider.setValue(params[static_cast<size_t>(base)].currentValue,
                                     juce::dontSendNotification);
        rows_[i].depthSlider.setValue(params[static_cast<size_t>(base + 1)].currentValue,
                                      juce::dontSendNotification);
    }
}

void FourOscUI::LFOTab::updatePluginState(const FourOscPluginState& state) {
    for (int i = 0; i < 2; ++i) {
        rows_[i].waveCombo.setSelectedId(state.lfoWaveShape[i] + 1, juce::dontSendNotification);
        rows_[i].syncButton.setToggleState(state.lfoSync[i], juce::dontSendNotification);
    }
}

void FourOscUI::LFOTab::setupLabel(juce::Label& label, const juce::String& text) {
    setupLabelStatic(label, text, this);
}

// =============================================================================
// FXTab
// =============================================================================

FourOscUI::FXTab::FXTab(FourOscUI& owner) : owner_(owner) {
    // Distortion
    distOnButton_.onClick = [this]() {
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("distortionOn", juce::var(distOnButton_.getToggleState()));
    };
    addAndMakeVisible(distOnButton_);
    setupLabel(distLabel_, "AMT");
    distAmountSlider_.setRange(0.0, 1.0, 0.001);
    distAmountSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kDistBase, static_cast<float>(v));
    };
    addAndMakeVisible(distAmountSlider_);

    // Reverb
    reverbOnButton_.onClick = [this]() {
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("reverbOn", juce::var(reverbOnButton_.getToggleState()));
    };
    addAndMakeVisible(reverbOnButton_);

    setupLabel(revSizeLabel_, "SIZE");
    reverbSizeSlider_.setRange(0.0, 1.0, 0.001);
    reverbSizeSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kReverbBase, static_cast<float>(v));
    };
    addAndMakeVisible(reverbSizeSlider_);

    setupLabel(revDampLabel_, "DAMP");
    reverbDampSlider_.setRange(0.0, 1.0, 0.001);
    reverbDampSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kReverbBase + 1, static_cast<float>(v));
    };
    addAndMakeVisible(reverbDampSlider_);

    setupLabel(revWidthLabel_, "WIDTH");
    reverbWidthSlider_.setRange(0.0, 1.0, 0.001);
    reverbWidthSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kReverbBase + 2, static_cast<float>(v));
    };
    addAndMakeVisible(reverbWidthSlider_);

    setupLabel(revMixLabel_, "MIX");
    reverbMixSlider_.setRange(0.0, 1.0, 0.001);
    reverbMixSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kReverbBase + 3, static_cast<float>(v));
    };
    addAndMakeVisible(reverbMixSlider_);

    // Delay
    delayOnButton_.onClick = [this]() {
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("delayOn", juce::var(delayOnButton_.getToggleState()));
    };
    addAndMakeVisible(delayOnButton_);

    setupLabel(delFbLabel_, "FB");
    delayFeedbackSlider_.setRange(-100.0, 0.0, 0.1);
    delayFeedbackSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kDelayBase, static_cast<float>(v));
    };
    addAndMakeVisible(delayFeedbackSlider_);

    setupLabel(delXfLabel_, "XFEED");
    delayCrossfeedSlider_.setRange(-100.0, 0.0, 0.1);
    delayCrossfeedSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kDelayBase + 1, static_cast<float>(v));
    };
    addAndMakeVisible(delayCrossfeedSlider_);

    setupLabel(delMixLabel_, "MIX");
    delayMixSlider_.setRange(0.0, 1.0, 0.001);
    delayMixSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kDelayBase + 2, static_cast<float>(v));
    };
    addAndMakeVisible(delayMixSlider_);

    // Chorus
    chorusOnButton_.onClick = [this]() {
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("chorusOn", juce::var(chorusOnButton_.getToggleState()));
    };
    addAndMakeVisible(chorusOnButton_);

    setupLabel(chSpeedLabel_, "SPD");
    chorusSpeedSlider_.setRange(0.1, 10.0, 0.01);
    chorusSpeedSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kChorusBase, static_cast<float>(v));
    };
    addAndMakeVisible(chorusSpeedSlider_);

    setupLabel(chDepthLabel_, "DEPTH");
    chorusDepthSlider_.setRange(0.1, 20.0, 0.01);
    chorusDepthSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kChorusBase + 1, static_cast<float>(v));
    };
    addAndMakeVisible(chorusDepthSlider_);

    setupLabel(chWidthLabel_, "WIDTH");
    chorusWidthSlider_.setRange(0.0, 1.0, 0.001);
    chorusWidthSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kChorusBase + 2, static_cast<float>(v));
    };
    addAndMakeVisible(chorusWidthSlider_);

    setupLabel(chMixLabel_, "MIX");
    chorusMixSlider_.setRange(0.0, 1.0, 0.001);
    chorusMixSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kChorusBase + 3, static_cast<float>(v));
    };
    addAndMakeVisible(chorusMixSlider_);
}

void FourOscUI::FXTab::resized() {
    auto area = getLocalBounds().reduced(4);
    constexpr int rowH = 22;
    constexpr int toggleW = 60;
    constexpr int labelW = 36;
    constexpr int sliderW = 46;
    constexpr int gap = 4;

    // Distortion row
    auto row1 = area.removeFromTop(rowH);
    distOnButton_.setBounds(row1.removeFromLeft(toggleW));
    row1.removeFromLeft(gap);
    distLabel_.setBounds(row1.removeFromLeft(labelW));
    row1.removeFromLeft(gap);
    distAmountSlider_.setBounds(row1.removeFromLeft(sliderW));
    area.removeFromTop(gap);

    // Reverb row
    auto row2 = area.removeFromTop(rowH);
    reverbOnButton_.setBounds(row2.removeFromLeft(toggleW));
    row2.removeFromLeft(gap);
    revSizeLabel_.setBounds(row2.removeFromLeft(labelW));
    row2.removeFromLeft(gap);
    reverbSizeSlider_.setBounds(row2.removeFromLeft(sliderW));
    row2.removeFromLeft(gap);
    revDampLabel_.setBounds(row2.removeFromLeft(labelW));
    row2.removeFromLeft(gap);
    reverbDampSlider_.setBounds(row2.removeFromLeft(sliderW));
    row2.removeFromLeft(gap);
    revWidthLabel_.setBounds(row2.removeFromLeft(labelW));
    row2.removeFromLeft(gap);
    reverbWidthSlider_.setBounds(row2.removeFromLeft(sliderW));
    row2.removeFromLeft(gap);
    revMixLabel_.setBounds(row2.removeFromLeft(labelW));
    row2.removeFromLeft(gap);
    reverbMixSlider_.setBounds(row2.removeFromLeft(sliderW));
    area.removeFromTop(gap);

    // Delay row
    auto row3 = area.removeFromTop(rowH);
    delayOnButton_.setBounds(row3.removeFromLeft(toggleW));
    row3.removeFromLeft(gap);
    delFbLabel_.setBounds(row3.removeFromLeft(labelW));
    row3.removeFromLeft(gap);
    delayFeedbackSlider_.setBounds(row3.removeFromLeft(sliderW));
    row3.removeFromLeft(gap);
    delXfLabel_.setBounds(row3.removeFromLeft(labelW));
    row3.removeFromLeft(gap);
    delayCrossfeedSlider_.setBounds(row3.removeFromLeft(sliderW));
    row3.removeFromLeft(gap);
    delMixLabel_.setBounds(row3.removeFromLeft(labelW));
    row3.removeFromLeft(gap);
    delayMixSlider_.setBounds(row3.removeFromLeft(sliderW));
    area.removeFromTop(gap);

    // Chorus row
    auto row4 = area.removeFromTop(rowH);
    chorusOnButton_.setBounds(row4.removeFromLeft(toggleW));
    row4.removeFromLeft(gap);
    chSpeedLabel_.setBounds(row4.removeFromLeft(labelW));
    row4.removeFromLeft(gap);
    chorusSpeedSlider_.setBounds(row4.removeFromLeft(sliderW));
    row4.removeFromLeft(gap);
    chDepthLabel_.setBounds(row4.removeFromLeft(labelW));
    row4.removeFromLeft(gap);
    chorusDepthSlider_.setBounds(row4.removeFromLeft(sliderW));
    row4.removeFromLeft(gap);
    chWidthLabel_.setBounds(row4.removeFromLeft(labelW));
    row4.removeFromLeft(gap);
    chorusWidthSlider_.setBounds(row4.removeFromLeft(sliderW));
    row4.removeFromLeft(gap);
    chMixLabel_.setBounds(row4.removeFromLeft(labelW));
    row4.removeFromLeft(gap);
    chorusMixSlider_.setBounds(row4.removeFromLeft(sliderW));
}

void FourOscUI::FXTab::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    if (kDistBase >= static_cast<int>(params.size()))
        return;
    distAmountSlider_.setValue(params[kDistBase].currentValue, juce::dontSendNotification);

    if (kReverbBase + 3 < static_cast<int>(params.size())) {
        reverbSizeSlider_.setValue(params[kReverbBase].currentValue, juce::dontSendNotification);
        reverbDampSlider_.setValue(params[kReverbBase + 1].currentValue,
                                   juce::dontSendNotification);
        reverbWidthSlider_.setValue(params[kReverbBase + 2].currentValue,
                                    juce::dontSendNotification);
        reverbMixSlider_.setValue(params[kReverbBase + 3].currentValue, juce::dontSendNotification);
    }

    if (kDelayBase + 2 < static_cast<int>(params.size())) {
        delayFeedbackSlider_.setValue(params[kDelayBase].currentValue, juce::dontSendNotification);
        delayCrossfeedSlider_.setValue(params[kDelayBase + 1].currentValue,
                                       juce::dontSendNotification);
        delayMixSlider_.setValue(params[kDelayBase + 2].currentValue, juce::dontSendNotification);
    }

    if (kChorusBase + 3 < static_cast<int>(params.size())) {
        chorusSpeedSlider_.setValue(params[kChorusBase].currentValue, juce::dontSendNotification);
        chorusDepthSlider_.setValue(params[kChorusBase + 1].currentValue,
                                    juce::dontSendNotification);
        chorusWidthSlider_.setValue(params[kChorusBase + 2].currentValue,
                                    juce::dontSendNotification);
        chorusMixSlider_.setValue(params[kChorusBase + 3].currentValue, juce::dontSendNotification);
    }
}

void FourOscUI::FXTab::updatePluginState(const FourOscPluginState& state) {
    distOnButton_.setToggleState(state.distortionOn, juce::dontSendNotification);
    reverbOnButton_.setToggleState(state.reverbOn, juce::dontSendNotification);
    delayOnButton_.setToggleState(state.delayOn, juce::dontSendNotification);
    chorusOnButton_.setToggleState(state.chorusOn, juce::dontSendNotification);
}

void FourOscUI::FXTab::setupLabel(juce::Label& label, const juce::String& text) {
    setupLabelStatic(label, text, this);
}

}  // namespace magda::daw::ui
