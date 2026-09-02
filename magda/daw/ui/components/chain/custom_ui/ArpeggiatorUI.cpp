#include "custom_ui/ArpeggiatorUI.hpp"

#include "ui/themes/SmallButtonLookAndFeel.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

using Arp = daw::audio::ArpeggiatorPlugin;

// Layout constants
static constexpr int ROW_HEIGHT = 22;
static constexpr int ROW_GAP = 4;
static constexpr int LABEL_WIDTH = 52;
static constexpr int PADDING = 6;
static constexpr int COLUMN_GAP = 10;

ArpeggiatorUI::ArpeggiatorUI() {
    // Left column
    setupLabel(patternLabel_, "PATTERN");
    setupCombo(patternCombo_);
    patternCombo_.addItem("Up", 1);
    patternCombo_.addItem("Down", 2);
    patternCombo_.addItem("Up/Down", 3);
    patternCombo_.addItem("Down/Up", 4);
    patternCombo_.addItem("Random", 5);
    patternCombo_.addItem("As Played", 6);
    patternCombo_.onChange = [this] {
        sendChange(Arp::kPattern, static_cast<float>(patternCombo_.getSelectedId() - 1));
    };

    static const char* rateNames[] = {"1/4.", "1/4",   "1/4T", "1/8.",  "1/8",
                                      "1/8T", "1/16.", "1/16", "1/16T", "1/32"};

    setupLabel(rateLabel_, "RATE");
    setupSlider(rateSlider_, 0, 9, 1);
    rateSlider_.setValueFormatter([](double v) {
        int idx = juce::jlimit(0, 9, juce::roundToInt(v));
        return juce::String(rateNames[idx]);
    });
    rateSlider_.setValueParser([](const juce::String&) { return 1.0; });
    rateSlider_.onValueChanged = [this](double value) {
        sendChange(Arp::kRate, static_cast<float>(juce::roundToInt(value)));
    };

    setupLabel(octavesLabel_, "OCTAVES");
    setupSlider(octavesSlider_, 1, 4, 1);
    octavesSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    octavesSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    octavesSlider_.onValueChanged = [this](double value) {
        sendChange(Arp::kOctaves, static_cast<float>(juce::roundToInt(value)));
    };

    setupLabel(latchLabel_, "LATCH");
    latchButton_.setClickingTogglesState(true);
    latchButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    latchButton_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    latchButton_.setColour(juce::TextButton::buttonOnColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.6f));
    latchButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    latchButton_.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    latchButton_.onClick = [this] {
        const bool on = latchButton_.getToggleState();
        latchButton_.setButtonText(on ? "ON" : "OFF");
        sendChange(Arp::kLatch, on ? 1.0f : 0.0f);
    };
    addAndMakeVisible(latchButton_);
    setupLabel(rampLabel_, "TIME BEND");
    rampCurveDisplay_.setTooltip("Drag the handle to shape note timing within each arpeggio cycle. "
                                 "Double-click to reset.");
    addAndMakeVisible(rampCurveDisplay_);

    // Right column
    setupLabel(gateLabel_, "GATE");
    setupSlider(gateSlider_, 0.01, 1.0, 0.01);
    gateSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    gateSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    gateSlider_.onValueChanged = [this](double value) {
        sendChange(Arp::kGate, static_cast<float>(value));
    };

    setupLabel(swingLabel_, "SWING");
    setupSlider(swingSlider_, 0.0, 1.0, 0.01);
    swingSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    swingSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    swingSlider_.onValueChanged = [this](double value) {
        sendChange(Arp::kSwing, static_cast<float>(value));
    };

    rampCurveDisplay_.setMouseCursor(juce::MouseCursor::CrosshairCursor);
    rampCurveDisplay_.onCurveChanged = [this](float depth, float sk) {
        sendChange(Arp::kRamp, depth);
        sendChange(Arp::kSkew, sk);
        depthSlider_.setValue(static_cast<double>(depth), juce::dontSendNotification);
        skewSlider_.setValue(static_cast<double>(sk), juce::dontSendNotification);
    };

    // Timing X/Y sliders (linkable via macros)
    setupLabel(depthLabel_, "DEPTH");
    setupSlider(depthSlider_, -1.0, 1.0, 0.01);
    depthSlider_.setValueFormatter([](double v) { return juce::String(v, 2); });
    depthSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    depthSlider_.onValueChanged = [this](double value) {
        sendChange(Arp::kRamp, static_cast<float>(value));
        rampCurveDisplay_.setValues(static_cast<float>(value), rampCurveDisplay_.getSkew());
    };

    setupLabel(skewLabel_, "SKEW");
    setupSlider(skewSlider_, -1.0, 1.0, 0.01);
    skewSlider_.setValueFormatter([](double v) { return juce::String(v, 2); });
    skewSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    skewSlider_.onValueChanged = [this](double value) {
        sendChange(Arp::kSkew, static_cast<float>(value));
        rampCurveDisplay_.setValues(rampCurveDisplay_.getDepth(), static_cast<float>(value));
    };

    setupLabel(cyclesLabel_, "CYCLES");
    setupSlider(cyclesSlider_, 1.0, 8.0, 1.0);
    cyclesSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    cyclesSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    cyclesSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->rampCycles.store(juce::roundToInt(value), std::memory_order_relaxed);
    };

    setupLabel(quantizeLabel_, "Q");
    setupSlider(quantizeSlider_, 0.0, 1.0, 0.01);
    quantizeSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });
    quantizeSlider_.setValueParser(
        [](const juce::String& t) { return t.trimCharactersAtEnd("%").getDoubleValue() / 100.0; });
    quantizeSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->quantize.store(static_cast<float>(value), std::memory_order_relaxed);
    };

    setupLabel(quantizeSubLabel_, "SUB");
    setupSlider(quantizeSubSlider_, 16, 512, 16);
    quantizeSubSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)); });
    quantizeSubSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    quantizeSubSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->quantizeSub.store(juce::roundToInt(value), std::memory_order_relaxed);
    };

    rampCurveDisplay_.onHardAngleChanged = [this](bool hardAngle) {
        if (plugin_)
            plugin_->hardAngle.store(hardAngle, std::memory_order_relaxed);
    };

    setupLabel(velModeLabel_, "VEL MODE");
    setupCombo(velModeCombo_);
    velModeCombo_.addItem("Original", 1);
    velModeCombo_.addItem("Fixed", 2);
    velModeCombo_.addItem("Accent", 3);
    velModeCombo_.onChange = [this] {
        const int mode = velModeCombo_.getSelectedId() - 1;
        sendChange(Arp::kVelMode, static_cast<float>(mode));
        const bool showFixed = static_cast<Arp::VelocityMode>(mode) == Arp::VelocityMode::Fixed;
        fixedVelSlider_.setEnabled(showFixed);
        fixedVelSlider_.setAlpha(showFixed ? 1.0f : 0.3f);
    };

    setupLabel(fixedVelLabel_, "FIXED VEL");
    setupSlider(fixedVelSlider_, 1, 127, 1);
    fixedVelSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    fixedVelSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    fixedVelSlider_.onValueChanged = [this](double value) {
        sendChange(Arp::kFixedVel, static_cast<float>(juce::roundToInt(value)));
    };
    fixedVelSlider_.setEnabled(false);
    fixedVelSlider_.setAlpha(0.3f);
}

ArpeggiatorUI::~ArpeggiatorUI() {
    stopTimer();
    latchButton_.setLookAndFeel(nullptr);
    patternCombo_.setLookAndFeel(nullptr);
    velModeCombo_.setLookAndFeel(nullptr);
}

void ArpeggiatorUI::sendChange(int paramIndex, float value) {
    if (onParameterChanged)
        onParameterChanged(paramIndex, value);
}

void ArpeggiatorUI::setArpeggiator(daw::audio::ArpeggiatorPlugin* device) {
    stopTimer();
    plugin_ = device;

    if (plugin_ != nullptr) {
        syncSettingsFromDevice();
        startTimerHz(30);
    }
}

void ArpeggiatorUI::syncSettingsFromDevice() {
    if (plugin_ == nullptr)
        return;

    cyclesSlider_.setValue(static_cast<double>(plugin_->rampCycles.load(std::memory_order_relaxed)),
                           juce::dontSendNotification);
    quantizeSlider_.setValue(static_cast<double>(plugin_->quantize.load(std::memory_order_relaxed)),
                             juce::dontSendNotification);
    quantizeSubSlider_.setValue(
        static_cast<double>(plugin_->quantizeSub.load(std::memory_order_relaxed)),
        juce::dontSendNotification);
    rampCurveDisplay_.setHardAngle(plugin_->hardAngle.load(std::memory_order_relaxed));
}

void ArpeggiatorUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    const auto display = [&params](int index, float fallback) {
        return index < static_cast<int>(params.size())
                   ? params[static_cast<size_t>(index)].currentValue
                   : fallback;
    };

    patternCombo_.setSelectedId(juce::roundToInt(display(Arp::kPattern, 0.0f)) + 1,
                                juce::dontSendNotification);
    rateSlider_.setValue(static_cast<double>(display(Arp::kRate, 4.0f)),
                         juce::dontSendNotification);
    octavesSlider_.setValue(static_cast<double>(display(Arp::kOctaves, 1.0f)),
                            juce::dontSendNotification);

    const bool latched = display(Arp::kLatch, 0.0f) >= 0.5f;
    latchButton_.setToggleState(latched, juce::dontSendNotification);
    latchButton_.setButtonText(latched ? "ON" : "OFF");

    gateSlider_.setValue(static_cast<double>(display(Arp::kGate, 0.8f)),
                         juce::dontSendNotification);
    swingSlider_.setValue(static_cast<double>(display(Arp::kSwing, 0.0f)),
                          juce::dontSendNotification);

    const float depth = display(Arp::kRamp, 0.0f);
    const float skew = display(Arp::kSkew, 0.0f);
    rampCurveDisplay_.setValues(depth, skew);
    depthSlider_.setValue(static_cast<double>(depth), juce::dontSendNotification);
    skewSlider_.setValue(static_cast<double>(skew), juce::dontSendNotification);

    const int velMode = juce::roundToInt(display(Arp::kVelMode, 0.0f));
    velModeCombo_.setSelectedId(velMode + 1, juce::dontSendNotification);
    fixedVelSlider_.setValue(static_cast<double>(display(Arp::kFixedVel, 100.0f)),
                             juce::dontSendNotification);

    const bool showFixed = static_cast<Arp::VelocityMode>(velMode) == Arp::VelocityMode::Fixed;
    fixedVelSlider_.setEnabled(showFixed);
    fixedVelSlider_.setAlpha(showFixed ? 1.0f : 0.3f);

    syncSettingsFromDevice();
}

void ArpeggiatorUI::timerCallback() {
    if (plugin_ == nullptr)
        return;

    // Modulated values: the host pushes the modulated slot positions into the
    // device every block, so its parameter mirror includes macro modulation.
    const auto slotDisplay = [this](int slot) {
        return magda::ParameterUtils::normalizedToReal(plugin_->parameterValue(slot),
                                                       plugin_->parameterInfo(slot));
    };
    const float depth = slotDisplay(Arp::kRamp);
    const float skew = slotDisplay(Arp::kSkew);

    rampCurveDisplay_.setValues(depth, skew);
    depthSlider_.setValue(static_cast<double>(depth), juce::dontSendNotification);
    skewSlider_.setValue(static_cast<double>(skew), juce::dontSendNotification);

    // Playback sweep animation
    int step = plugin_->currentPlayStep_.load(std::memory_order_relaxed);
    int len = plugin_->currentSeqLength_.load(std::memory_order_relaxed);
    if (len > 0)
        rampCurveDisplay_.setNumTicks(len);
    int cycles =
        juce::jlimit(1, std::max(1, len), plugin_->rampCycles.load(std::memory_order_relaxed));
    float pos = (step >= 0 && len > 0) ? static_cast<float>(step) / static_cast<float>(len) : -1.0f;
    rampCurveDisplay_.setPlaybackPosition(pos, cycles);
}

void ArpeggiatorUI::paint(juce::Graphics&) {
    // No chrome — content is laid out directly
}

void ArpeggiatorUI::resized() {
    auto bounds = getLocalBounds().reduced(PADDING);
    int colWidth = (bounds.getWidth() - COLUMN_GAP) / 2;

    // Helper to layout a label + control row
    auto layoutRow = [](juce::Rectangle<int>& col, juce::Label& label, juce::Component& control) {
        auto row = col.removeFromTop(ROW_HEIGHT);
        label.setBounds(row.removeFromLeft(LABEL_WIDTH));
        control.setBounds(row);
        col.removeFromTop(ROW_GAP);
    };

    // Two-column top section (4 rows each)
    int topRowsHeight = 4 * (ROW_HEIGHT + ROW_GAP);
    auto topSection = bounds.removeFromTop(topRowsHeight);
    topSectionBottom_ = topSection.getBottom();

    auto leftCol = topSection.removeFromLeft(colWidth);
    topSection.removeFromLeft(COLUMN_GAP);
    auto rightCol = topSection;

    // Left column
    layoutRow(leftCol, patternLabel_, patternCombo_);
    layoutRow(leftCol, rateLabel_, rateSlider_);
    layoutRow(leftCol, octavesLabel_, octavesSlider_);
    layoutRow(leftCol, latchLabel_, latchButton_);

    // Right column
    layoutRow(rightCol, gateLabel_, gateSlider_);
    layoutRow(rightCol, swingLabel_, swingSlider_);
    layoutRow(rightCol, velModeLabel_, velModeCombo_);
    layoutRow(rightCol, fixedVelLabel_, fixedVelSlider_);

    // Ease section: two-column row matching above, then full-width curve display
    bounds.removeFromTop(ROW_GAP);
    auto easeRow = bounds.removeFromTop(ROW_HEIGHT);
    {
        auto easeLeft = easeRow.removeFromLeft(colWidth);
        easeRow.removeFromLeft(COLUMN_GAP);
        auto easeRight = easeRow;

        depthLabel_.setBounds(easeLeft.removeFromLeft(LABEL_WIDTH));
        depthSlider_.setBounds(easeLeft);

        skewLabel_.setBounds(easeRight.removeFromLeft(LABEL_WIDTH));
        skewSlider_.setBounds(easeRight);
    }
    bounds.removeFromTop(ROW_GAP);
    auto rampLabelRow = bounds.removeFromTop(ROW_HEIGHT);
    {
        auto cyclesArea = rampLabelRow.removeFromRight(100);
        rampLabel_.setBounds(rampLabelRow);
        cyclesLabel_.setBounds(cyclesArea.removeFromLeft(50));
        cyclesSlider_.setBounds(cyclesArea);
    }
    bounds.removeFromTop(ROW_GAP);
    auto quantizeRow = bounds.removeFromTop(ROW_HEIGHT);
    {
        auto qLeft = quantizeRow.removeFromLeft(colWidth);
        quantizeRow.removeFromLeft(COLUMN_GAP);
        auto qRight = quantizeRow;
        quantizeLabel_.setBounds(qLeft.removeFromLeft(LABEL_WIDTH));
        quantizeSlider_.setBounds(qLeft);
        quantizeSubLabel_.setBounds(qRight.removeFromLeft(LABEL_WIDTH));
        quantizeSubSlider_.setBounds(qRight);
    }
    bounds.removeFromTop(ROW_GAP);
    if (bounds.getHeight() > 20)
        rampCurveDisplay_.setBounds(bounds);
}

void ArpeggiatorUI::setupLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
}

void ArpeggiatorUI::setupCombo(juce::ComboBox& combo) {
    combo.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    combo.setColour(juce::ComboBox::backgroundColourId,
                    DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    combo.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    combo.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    addAndMakeVisible(combo);
}

void ArpeggiatorUI::setupSlider(LinkableTextSlider& slider, double min, double max, double step) {
    slider.setRange(min, max, step);
    addAndMakeVisible(slider);
}

void ArpeggiatorUI::lookAndFeelChanged() {
    // Re-apply cached theme colours after a live theme switch.
    for (auto* label :
         {&patternLabel_, &rateLabel_, &octavesLabel_, &latchLabel_, &rampLabel_, &depthLabel_,
          &skewLabel_, &cyclesLabel_, &quantizeLabel_, &quantizeSubLabel_, &gateLabel_,
          &swingLabel_, &velModeLabel_, &fixedVelLabel_})
        label->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());

    for (auto* combo : {&patternCombo_, &velModeCombo_}) {
        combo->setColour(juce::ComboBox::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
        combo->setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
        combo->setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    }

    latchButton_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    latchButton_.setColour(juce::TextButton::buttonOnColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.6f));
    latchButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    latchButton_.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());

    repaint();
}

std::vector<LinkableTextSlider*> ArpeggiatorUI::getLinkableSliders() {
    // Pre-set param indices to match the device's slot order:
    // 0=pattern, 1=rate, 2=octaves, 3=gate, 4=swing, 5=ramp, 6=skew, 7=latch, 8=velMode, 9=fixedVel
    // setupCustomUILinking() will use these indices (via getParamIndex()) instead of vector
    // position.
    magda::ChainNodePath dummy;
    rateSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Arp::kRate, dummy);
    octavesSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Arp::kOctaves, dummy);
    gateSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Arp::kGate, dummy);
    swingSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Arp::kSwing, dummy);
    depthSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Arp::kRamp, dummy);
    skewSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Arp::kSkew, dummy);
    fixedVelSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Arp::kFixedVel, dummy);
    return {&rateSlider_,  &octavesSlider_, &gateSlider_,    &swingSlider_,
            &depthSlider_, &skewSlider_,    &fixedVelSlider_};
}

}  // namespace magda::daw::ui
