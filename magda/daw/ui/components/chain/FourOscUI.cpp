#include "FourOscUI.hpp"

#include "BinaryData.h"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// =============================================================================
// FourOscUI
// =============================================================================

FourOscUI::FourOscUI() {
    tabs_ = std::make_unique<LayoutStableTabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
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

int FourOscUI::getCurrentTabIndex() const {
    return tabs_ ? tabs_->getCurrentTabIndex() : 0;
}

void FourOscUI::setCurrentTabIndex(int index) {
    if (tabs_ && index >= 0 && index < tabs_->getNumTabs())
        tabs_->setCurrentTabIndex(index, false);
}

void FourOscUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void FourOscUI::resized() {
    tabs_->setBoundsStable(getLocalBounds());
}

std::vector<LinkableTextSlider*> FourOscUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> sliders;

    // OSC tab: 4 oscillators x 7 params each (indices 0-27)
    for (int i = 0; i < 4; ++i) {
        auto& row = oscTab_->rows_[i];
        sliders.push_back(&row.tuneSlider);        // oscBase + i*7 + 0
        sliders.push_back(&row.fineSlider);        // oscBase + i*7 + 1
        sliders.push_back(&row.levelSlider);       // oscBase + i*7 + 2
        sliders.push_back(&row.pulseWidthSlider);  // oscBase + i*7 + 3
        sliders.push_back(&row.detuneSlider);      // oscBase + i*7 + 4
        sliders.push_back(&row.spreadSlider);      // oscBase + i*7 + 5
        sliders.push_back(&row.panSlider);         // oscBase + i*7 + 6
    }

    // LFO tab: 2 LFOs x 2 params each (indices 28-31)
    for (int i = 0; i < 2; ++i) {
        auto& row = lfoTab_->rows_[i];
        sliders.push_back(&row.rateSlider);   // lfoBase + i*2 + 0
        sliders.push_back(&row.depthSlider);  // lfoBase + i*2 + 1
    }

    // Mod Env tab: 2 envelopes x 4 params each (indices 32-39)
    for (int i = 0; i < 2; ++i) {
        auto& row = modEnvTab_->rows_[i];
        sliders.push_back(&row.attackSlider);   // modEnvBase + i*4 + 0
        sliders.push_back(&row.decaySlider);    // modEnvBase + i*4 + 1
        sliders.push_back(&row.sustainSlider);  // modEnvBase + i*4 + 2
        sliders.push_back(&row.releaseSlider);  // modEnvBase + i*4 + 3
    }

    // Amp tab: 5 params (indices 40-44)
    sliders.push_back(&ampTab_->attackSlider_);    // ampBase + 0
    sliders.push_back(&ampTab_->decaySlider_);     // ampBase + 1
    sliders.push_back(&ampTab_->sustainSlider_);   // ampBase + 2
    sliders.push_back(&ampTab_->releaseSlider_);   // ampBase + 3
    sliders.push_back(&ampTab_->velocitySlider_);  // ampBase + 4

    // Filter tab: 9 params (indices 45-53)
    sliders.push_back(&filterTab_->attackSlider_);     // filterBase + 0
    sliders.push_back(&filterTab_->decaySlider_);      // filterBase + 1
    sliders.push_back(&filterTab_->sustainSlider_);    // filterBase + 2
    sliders.push_back(&filterTab_->releaseSlider_);    // filterBase + 3
    sliders.push_back(&filterTab_->freqSlider_);       // filterBase + 4
    sliders.push_back(&filterTab_->resonanceSlider_);  // filterBase + 5
    sliders.push_back(&filterTab_->amountSlider_);     // filterBase + 6
    sliders.push_back(&filterTab_->keySlider_);        // filterBase + 7
    sliders.push_back(&filterTab_->velocitySlider_);   // filterBase + 8

    // Distortion: 1 param (index 54)
    sliders.push_back(&fxTab_->distAmountSlider_);  // distBase + 0

    // Reverb: 4 params (indices 55-58)
    sliders.push_back(&fxTab_->reverbSizeSlider_);   // reverbBase + 0
    sliders.push_back(&fxTab_->reverbDampSlider_);   // reverbBase + 1
    sliders.push_back(&fxTab_->reverbWidthSlider_);  // reverbBase + 2
    sliders.push_back(&fxTab_->reverbMixSlider_);    // reverbBase + 3

    // Delay: 3 params (indices 59-61)
    sliders.push_back(&fxTab_->delayFeedbackSlider_);   // delayBase + 0
    sliders.push_back(&fxTab_->delayCrossfeedSlider_);  // delayBase + 1
    sliders.push_back(&fxTab_->delayMixSlider_);        // delayBase + 2

    // Chorus: 4 params (indices 62-65)
    sliders.push_back(&fxTab_->chorusSpeedSlider_);  // chorusBase + 0
    sliders.push_back(&fxTab_->chorusDepthSlider_);  // chorusBase + 1
    sliders.push_back(&fxTab_->chorusWidthSlider_);  // chorusBase + 2
    sliders.push_back(&fxTab_->chorusMixSlider_);    // chorusBase + 3

    // Global: 2 params (indices 66-67)
    sliders.push_back(&oscTab_->legatoSlider_);       // globalBase + 0
    sliders.push_back(&oscTab_->masterLevelSlider_);  // globalBase + 1

    return sliders;
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
// Helper: load ADSR icon
// =============================================================================

// =============================================================================
// Helper: populate waveform icon selector (shared by OscTab + LFOTab)
// =============================================================================

static void populateWaveSelector(IconSelector& selector) {
    // Matches Oscillator::Waves enum: none=0, sine=1, square=2, saw=3, triangle=4, noise=5
    selector.addTextOption("Off", "Off");
    selector.addOption(BinaryData::fadmodsine_svg, BinaryData::fadmodsine_svgSize, "Sine");
    selector.addOption(BinaryData::fadmodsquare_svg, BinaryData::fadmodsquare_svgSize, "Square");
    selector.addOption(BinaryData::fadmodsawup_svg, BinaryData::fadmodsawup_svgSize, "Saw");
    selector.addOption(BinaryData::fadmodtri_svg, BinaryData::fadmodtri_svgSize, "Triangle");
    selector.addOption(BinaryData::fadmodrandom_svg, BinaryData::fadmodrandom_svgSize, "Noise");
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

        setupLabel(row.label, juce::String(i + 1));

        // Waveform icon selector
        setupWaveSelector(row.waveSelector);
        row.waveSelector.onChange = [this, i](int shape) {
            DBG("OscTab wave selector changed: osc=" + juce::String(i + 1) +
                " shape=" + juce::String(shape));
            if (owner_.onPluginStateChanged)
                owner_.onPluginStateChanged("waveShape" + juce::String(i + 1), juce::var(shape));
        };
        addAndMakeVisible(row.waveSelector);

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

        // Voices slider (1-8 integer)
        row.voicesSlider.setRange(1.0, 8.0, 1.0);
        row.voicesSlider.setValue(1.0, juce::dontSendNotification);
        row.voicesSlider.setValueFormatter(
            [](double v) { return juce::String(static_cast<int>(v)); });
        row.voicesSlider.onValueChanged = [this, i](double v) {
            if (owner_.onPluginStateChanged)
                owner_.onPluginStateChanged("voices" + juce::String(i + 1),
                                            juce::var(static_cast<int>(v)));
        };
        addAndMakeVisible(row.voicesSlider);
    }

    // Global controls row
    setupLabel(modeLabel_, "MODE");
    voiceModeSelector_.addTextOption("Mono", "Mono");
    voiceModeSelector_.addTextOption("Leg", "Legato");
    voiceModeSelector_.addTextOption("Poly", "Poly");
    voiceModeSelector_.setSelectedIndex(2, juce::dontSendNotification);  // Default Poly
    voiceModeSelector_.onChange = [this](int idx) {
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("voiceMode", juce::var(idx));
    };
    addAndMakeVisible(voiceModeSelector_);

    setupLabel(gVoicesLabel_, "VOICES");
    globalVoicesSlider_.setRange(1.0, 64.0, 1.0);
    globalVoicesSlider_.setValue(32.0, juce::dontSendNotification);
    globalVoicesSlider_.setValueFormatter(
        [](double v) { return juce::String(static_cast<int>(v)); });
    globalVoicesSlider_.onValueChanged = [this](double v) {
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("voices", juce::var(static_cast<int>(v)));
    };
    addAndMakeVisible(globalVoicesSlider_);

    setupLabel(legatoLabel_, "GLIDE");
    legatoSlider_.setRange(0.0, 500.0, 0.1);
    legatoSlider_.setValue(0.0, juce::dontSendNotification);
    legatoSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kGlobalBase, static_cast<float>(v));
    };
    addAndMakeVisible(legatoSlider_);

    setupLabel(masterLabel_, "LEVEL");
    masterLevelSlider_.setRange(-100.0, 0.0, 0.1);
    masterLevelSlider_.setValue(0.0, juce::dontSendNotification);
    masterLevelSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kGlobalBase + 1, static_cast<float>(v));
    };
    addAndMakeVisible(masterLevelSlider_);
}

void FourOscUI::OscTab::setupWaveSelector(IconSelector& selector) {
    populateWaveSelector(selector);
}

void FourOscUI::OscTab::resized() {
    auto area = getLocalBounds().reduced(4);
    constexpr int rowH = 22;
    constexpr int labelW = 16;
    constexpr int waveSelectorW = 96;  // 6 items (Off + 5 waveforms) at 16px each
    constexpr int sliderW = 42;
    constexpr int comboW = 46;
    constexpr int gap = 2;

    // Header row
    auto headerRow = area.removeFromTop(14);
    headerRow.removeFromLeft(labelW + gap);
    hdrWave_.setBounds(headerRow.removeFromLeft(waveSelectorW));
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
        row.waveSelector.setBounds(rowArea.removeFromLeft(waveSelectorW));
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
        row.voicesSlider.setBounds(rowArea.removeFromLeft(comboW));
    }

    // Global controls row at the bottom
    area.removeFromTop(4);
    constexpr int globalRowH = 22;
    constexpr int modeSelectorW = 72;  // 3 text options
    constexpr int globalSliderW = 50;
    constexpr int globalLabelW = 36;

    auto globalRow = area.removeFromTop(globalRowH);
    modeLabel_.setBounds(globalRow.removeFromLeft(globalLabelW));
    globalRow.removeFromLeft(gap);
    voiceModeSelector_.setBounds(globalRow.removeFromLeft(modeSelectorW));
    globalRow.removeFromLeft(gap * 3);
    gVoicesLabel_.setBounds(globalRow.removeFromLeft(globalLabelW));
    globalRow.removeFromLeft(gap);
    globalVoicesSlider_.setBounds(globalRow.removeFromLeft(globalSliderW));
    globalRow.removeFromLeft(gap * 3);
    legatoLabel_.setBounds(globalRow.removeFromLeft(globalLabelW));
    globalRow.removeFromLeft(gap);
    legatoSlider_.setBounds(globalRow.removeFromLeft(globalSliderW));
    globalRow.removeFromLeft(gap * 3);
    masterLabel_.setBounds(globalRow.removeFromLeft(globalLabelW));
    globalRow.removeFromLeft(gap);
    masterLevelSlider_.setBounds(globalRow.removeFromLeft(globalSliderW));
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
    // Global automatable params
    if (kGlobalBase + 1 < static_cast<int>(params.size())) {
        legatoSlider_.setValue(params[kGlobalBase].currentValue, juce::dontSendNotification);
        masterLevelSlider_.setValue(params[kGlobalBase + 1].currentValue,
                                    juce::dontSendNotification);
    }
}

void FourOscUI::OscTab::updatePluginState(const FourOscPluginState& state) {
    for (int i = 0; i < 4; ++i) {
        rows_[i].waveSelector.setSelectedIndex(state.oscWaveShape[i], juce::dontSendNotification);
        rows_[i].voicesSlider.setValue(static_cast<double>(juce::jlimit(1, 8, state.oscVoices[i])),
                                       juce::dontSendNotification);
    }
    voiceModeSelector_.setSelectedIndex(juce::jlimit(0, 2, state.voiceMode),
                                        juce::dontSendNotification);
    globalVoicesSlider_.setValue(static_cast<double>(juce::jlimit(1, 64, state.globalVoices)),
                                 juce::dontSendNotification);
}

void FourOscUI::OscTab::setupLabel(juce::Label& label, const juce::String& text) {
    setupLabelStatic(label, text, this);
}

// =============================================================================
// FilterTab
// =============================================================================

FourOscUI::FilterTab::FilterTab(FourOscUI& owner) : owner_(owner) {
    // Filter type icon selector: Off, LP, HP, BP, Notch
    setupLabel(typeLabel_, "TYPE");
    typeSelector_.addOption(BinaryData::fadfilterbypass_svg, BinaryData::fadfilterbypass_svgSize,
                            "Off");
    typeSelector_.addOption(BinaryData::fadfilterlowpass_svg, BinaryData::fadfilterlowpass_svgSize,
                            "Low Pass");
    typeSelector_.addOption(BinaryData::fadfilterhighpass_svg,
                            BinaryData::fadfilterhighpass_svgSize, "High Pass");
    typeSelector_.addOption(BinaryData::fadfilterbandpass_svg,
                            BinaryData::fadfilterbandpass_svgSize, "Band Pass");
    typeSelector_.addOption(BinaryData::fadfilternotch_svg, BinaryData::fadfilternotch_svgSize,
                            "Notch");
    typeSelector_.setSelectedIndex(1, juce::dontSendNotification);  // Default LP
    typeSelector_.onChange = [this](int idx) {
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("filterType", juce::var(idx));
    };
    addAndMakeVisible(typeSelector_);

    // Slope selector (text buttons)
    setupLabel(slopeLabel_, "SLOPE");
    slopeSelector_.addTextOption("12dB");
    slopeSelector_.addTextOption("24dB");
    slopeSelector_.setSelectedIndex(0, juce::dontSendNotification);
    slopeSelector_.onChange = [this](int idx) {
        int slope = (idx == 0) ? 12 : 24;
        if (owner_.onPluginStateChanged)
            owner_.onPluginStateChanged("filterSlope", juce::var(slope));
    };
    addAndMakeVisible(slopeSelector_);

    // Freq (internal value is MIDI note number, display as Hz)
    setupLabel(freqLabel_, "FREQ");
    freqSlider_.setRange(0.0, 135.076232, 0.01);
    freqSlider_.setSkewForCentre(69.0);  // Log-scale drag centred at A4 (440 Hz)
    freqSlider_.setValue(69.0, juce::dontSendNotification);
    freqSlider_.setValueFormatter([](double noteNum) -> juce::String {
        float freq = 440.0f * std::pow(2.0f, (static_cast<float>(noteNum) - 69.0f) / 12.0f);
        if (freq >= 1000.0f)
            return juce::String(freq / 1000.0f, 1) + " kHz";
        return juce::String(juce::roundToInt(freq)) + " Hz";
    });
    freqSlider_.setValueParser([](const juce::String& text) -> double {
        auto t = text.trim().toLowerCase();
        float freq;
        if (t.contains("khz"))
            freq = t.replace("khz", "").trim().getFloatValue() * 1000.0f;
        else
            freq = t.replace("hz", "").trim().getFloatValue();
        if (freq <= 0.0f)
            freq = 440.0f;
        return 12.0 * std::log2(freq / 440.0) + 69.0;
    });
    freqSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 4, static_cast<float>(v));
    };
    addAndMakeVisible(freqSlider_);

    // Resonance
    setupLabel(resLabel_, "RES");
    resonanceSlider_.setRange(0.0, 100.0, 0.1);
    resonanceSlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 5, static_cast<float>(v));
    };
    addAndMakeVisible(resonanceSlider_);

    // Key tracking
    setupLabel(keyLabel_, "KEY");
    keySlider_.setRange(0.0, 100.0, 0.1);
    keySlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 7, static_cast<float>(v));
    };
    addAndMakeVisible(keySlider_);

    // Velocity
    setupLabel(velLabel_, "VEL");
    velocitySlider_.setRange(0.0, 100.0, 0.1);
    velocitySlider_.onValueChanged = [this](double v) {
        if (owner_.onParameterChanged)
            owner_.onParameterChanged(kFilterBase + 8, static_cast<float>(v));
    };
    addAndMakeVisible(velocitySlider_);

    // Env amount
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
    constexpr int selectorW = 100;      // 5 filter type icons
    constexpr int slopeSelectorW = 60;  // 2 text options
    constexpr int sliderW = 50;
    constexpr int gap = 4;

    // Row 1: Type selector, Slope combo
    auto row1 = area.removeFromTop(rowH);
    typeLabel_.setBounds(row1.removeFromLeft(labelW));
    row1.removeFromLeft(gap);
    typeSelector_.setBounds(row1.removeFromLeft(selectorW));
    row1.removeFromLeft(gap + 8);
    slopeLabel_.setBounds(row1.removeFromLeft(labelW));
    row1.removeFromLeft(gap);
    slopeSelector_.setBounds(row1.removeFromLeft(slopeSelectorW));
    area.removeFromTop(gap);

    // Row 2: Freq, Resonance, Key
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

    // Row 4: Filter ADSR with icon
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

void FourOscUI::FilterTab::paint(juce::Graphics&) {}

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
    typeSelector_.setSelectedIndex(state.filterType, juce::dontSendNotification);
    slopeSelector_.setSelectedIndex(state.filterSlope == 24 ? 1 : 0, juce::dontSendNotification);
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

void FourOscUI::AmpTab::paint(juce::Graphics&) {}

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

void FourOscUI::ModEnvTab::paint(juce::Graphics&) {}

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

        // Wave icon selector
        setupWaveSelector(row.waveSelector);
        row.waveSelector.onChange = [this, i](int shape) {
            if (owner_.onPluginStateChanged)
                owner_.onPluginStateChanged("lfoShape" + juce::String(i + 1), juce::var(shape));
        };
        addAndMakeVisible(row.waveSelector);

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

void FourOscUI::LFOTab::setupWaveSelector(IconSelector& selector) {
    populateWaveSelector(selector);
}

void FourOscUI::LFOTab::resized() {
    auto area = getLocalBounds().reduced(4);
    constexpr int rowH = 22;
    constexpr int labelW = 36;
    constexpr int waveSelectorW = 112;  // 7 items (Off + 6 waveforms)
    constexpr int sliderW = 50;
    constexpr int toggleW = 50;
    constexpr int gap = 4;

    // Header
    auto headerRow = area.removeFromTop(14);
    headerRow.removeFromLeft(labelW + gap);
    hdrWave_.setBounds(headerRow.removeFromLeft(waveSelectorW));
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
        row.waveSelector.setBounds(rowArea.removeFromLeft(waveSelectorW));
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
        rows_[i].waveSelector.setSelectedIndex(state.lfoWaveShape[i], juce::dontSendNotification);
        rows_[i].syncButton.setToggleState(state.lfoSync[i], juce::dontSendNotification);
    }
}

void FourOscUI::LFOTab::setupLabel(juce::Label& label, const juce::String& text) {
    setupLabelStatic(label, text, this);
}

// =============================================================================
// FXTab
// =============================================================================

// LookAndFeel override so FX toggle buttons use the theme font
struct FXToggleLookAndFeel : juce::LookAndFeel_V4 {
    void drawTickBox(juce::Graphics& g, juce::Component& component, float x, float y, float w,
                     float h, bool ticked, bool isEnabled, bool /*shouldDrawButtonAsHighlighted*/,
                     bool /*shouldDrawButtonAsDown*/) override {
        auto boxBounds = juce::Rectangle<float>(x, y, w, h);
        g.setColour(component.findColour(juce::ToggleButton::tickDisabledColourId));
        g.drawRoundedRectangle(boxBounds, 2.0f, 1.0f);
        if (ticked) {
            g.setColour(isEnabled ? component.findColour(juce::ToggleButton::tickColourId)
                                  : component.findColour(juce::ToggleButton::tickDisabledColourId));
            auto tick = boxBounds.reduced(3.0f);
            g.fillRoundedRectangle(tick, 1.0f);
        }
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override {
        float fontSize = juce::jmin(11.0f, static_cast<float>(button.getHeight()) * 0.6f);
        float tickWidth = fontSize * 1.4f;

        drawTickBox(g, button, 4.0f, (static_cast<float>(button.getHeight()) - tickWidth) * 0.5f,
                    tickWidth, tickWidth, button.getToggleState(), button.isEnabled(),
                    shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(FontManager::getInstance().getUIFont(fontSize));

        if (!button.isEnabled())
            g.setOpacity(0.5f);

        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds()
                             .withTrimmedLeft(juce::roundToInt(tickWidth) + 10)
                             .withTrimmedRight(2),
                         juce::Justification::centredLeft, 10);
    }

    static FXToggleLookAndFeel& getInstance() {
        static FXToggleLookAndFeel instance;
        return instance;
    }
};

FourOscUI::FXTab::FXTab(FourOscUI& owner) : owner_(owner) {
    // Distortion
    distOnButton_.setLookAndFeel(&FXToggleLookAndFeel::getInstance());
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
    reverbOnButton_.setLookAndFeel(&FXToggleLookAndFeel::getInstance());
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
    delayOnButton_.setLookAndFeel(&FXToggleLookAndFeel::getInstance());
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
    chorusOnButton_.setLookAndFeel(&FXToggleLookAndFeel::getInstance());
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
    constexpr int toggleW = 50;
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
