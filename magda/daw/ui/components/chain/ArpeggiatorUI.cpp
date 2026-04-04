#include "ArpeggiatorUI.hpp"

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
        if (plugin_)
            plugin_->pattern = patternCombo_.getSelectedId() - 1;
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
        if (plugin_)
            plugin_->rate = juce::roundToInt(value);
    };

    setupLabel(octavesLabel_, "OCTAVES");
    setupSlider(octavesSlider_, 1, 4, 1);
    octavesSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    octavesSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    octavesSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->octaveRange = juce::roundToInt(value);
    };

    setupLabel(latchLabel_, "LATCH");
    latchButton_.setClickingTogglesState(true);
    latchButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    latchButton_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    latchButton_.setColour(juce::TextButton::buttonOnColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.6f));
    latchButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    latchButton_.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    latchButton_.onClick = [this] {
        if (plugin_) {
            bool on = latchButton_.getToggleState();
            plugin_->latch = on;
            latchButton_.setButtonText(on ? "ON" : "OFF");
        }
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
        if (plugin_)
            plugin_->gate = static_cast<float>(value);
    };

    setupLabel(swingLabel_, "SWING");
    setupSlider(swingSlider_, 0.0, 1.0, 0.01);
    swingSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    swingSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    swingSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->swing = static_cast<float>(value);
    };

    rampCurveDisplay_.setMouseCursor(juce::MouseCursor::CrosshairCursor);
    rampCurveDisplay_.onCurveChanged = [this](float depth, float sk) {
        if (plugin_) {
            plugin_->ramp = depth;
            plugin_->skew = sk;
        }
        depthSlider_.setValue(static_cast<double>(depth), juce::dontSendNotification);
        skewSlider_.setValue(static_cast<double>(sk), juce::dontSendNotification);
    };

    // Timing X/Y sliders (linkable via macros)
    setupLabel(depthLabel_, "DEPTH");
    setupSlider(depthSlider_, -1.0, 1.0, 0.01);
    depthSlider_.setValueFormatter([](double v) { return juce::String(v, 2); });
    depthSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    depthSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->ramp = static_cast<float>(value);
        rampCurveDisplay_.setValues(static_cast<float>(value), rampCurveDisplay_.getSkew());
    };

    setupLabel(skewLabel_, "SKEW");
    setupSlider(skewSlider_, -1.0, 1.0, 0.01);
    skewSlider_.setValueFormatter([](double v) { return juce::String(v, 2); });
    skewSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    skewSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->skew = static_cast<float>(value);
        rampCurveDisplay_.setValues(rampCurveDisplay_.getDepth(), static_cast<float>(value));
    };

    setupLabel(cyclesLabel_, "CYCLES");
    setupSlider(cyclesSlider_, 1.0, 8.0, 1.0);
    cyclesSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    cyclesSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    cyclesSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->rampCycles = juce::roundToInt(value);
    };

    setupLabel(quantizeLabel_, "Q");
    setupSlider(quantizeSlider_, 0.0, 1.0, 0.01);
    quantizeSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });
    quantizeSlider_.setValueParser(
        [](const juce::String& t) { return t.trimCharactersAtEnd("%").getDoubleValue() / 100.0; });
    quantizeSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->quantize = static_cast<float>(value);
    };

    setupLabel(quantizeSubLabel_, "SUB");
    setupSlider(quantizeSubSlider_, 16, 512, 16);
    quantizeSubSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)); });
    quantizeSubSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    quantizeSubSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->quantizeSub = juce::roundToInt(value);
    };

    rampCurveDisplay_.onHardAngleChanged = [this](bool hardAngle) {
        if (plugin_)
            plugin_->hardAngle = hardAngle;
    };

    setupLabel(velModeLabel_, "VEL MODE");
    setupCombo(velModeCombo_);
    velModeCombo_.addItem("Original", 1);
    velModeCombo_.addItem("Fixed", 2);
    velModeCombo_.addItem("Accent", 3);
    velModeCombo_.onChange = [this] {
        if (plugin_) {
            plugin_->velocityMode = velModeCombo_.getSelectedId() - 1;
            bool showFixed = static_cast<Arp::VelocityMode>(plugin_->velocityMode.get()) ==
                             Arp::VelocityMode::Fixed;
            fixedVelSlider_.setEnabled(showFixed);
            fixedVelSlider_.setAlpha(showFixed ? 1.0f : 0.3f);
        }
    };

    setupLabel(fixedVelLabel_, "FIXED VEL");
    setupSlider(fixedVelSlider_, 1, 127, 1);
    fixedVelSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    fixedVelSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    fixedVelSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->fixedVelocity = juce::roundToInt(value);
    };
    fixedVelSlider_.setEnabled(false);
    fixedVelSlider_.setAlpha(0.3f);
}

ArpeggiatorUI::~ArpeggiatorUI() {
    stopTimer();
    if (watchedState_.isValid())
        watchedState_.removeListener(this);
    latchButton_.setLookAndFeel(nullptr);
    patternCombo_.setLookAndFeel(nullptr);
    velModeCombo_.setLookAndFeel(nullptr);
}

void ArpeggiatorUI::setArpeggiator(daw::audio::ArpeggiatorPlugin* plugin) {
    stopTimer();
    if (watchedState_.isValid())
        watchedState_.removeListener(this);

    plugin_ = plugin;

    if (plugin_) {
        watchedState_ = plugin_->state;
        watchedState_.addListener(this);
        syncFromPlugin();
        startTimerHz(30);
    }
}

void ArpeggiatorUI::syncFromPlugin() {
    if (!plugin_)
        return;

    patternCombo_.setSelectedId(plugin_->pattern.get() + 1, juce::dontSendNotification);
    rateSlider_.setValue(static_cast<double>(plugin_->rate.get()), juce::dontSendNotification);
    octavesSlider_.setValue(static_cast<double>(plugin_->octaveRange.get()),
                            juce::dontSendNotification);

    bool latched = plugin_->latch.get();
    latchButton_.setToggleState(latched, juce::dontSendNotification);
    latchButton_.setButtonText(latched ? "ON" : "OFF");

    gateSlider_.setValue(static_cast<double>(plugin_->gate.get()), juce::dontSendNotification);
    swingSlider_.setValue(static_cast<double>(plugin_->swing.get()), juce::dontSendNotification);
    rampCurveDisplay_.setValues(plugin_->ramp.get(), plugin_->skew.get());
    depthSlider_.setValue(static_cast<double>(plugin_->ramp.get()), juce::dontSendNotification);
    skewSlider_.setValue(static_cast<double>(plugin_->skew.get()), juce::dontSendNotification);
    cyclesSlider_.setValue(static_cast<double>(plugin_->rampCycles.get()),
                           juce::dontSendNotification);
    rampCurveDisplay_.setHardAngle(plugin_->hardAngle.get());
    quantizeSlider_.setValue(static_cast<double>(plugin_->quantize.get()),
                             juce::dontSendNotification);
    quantizeSubSlider_.setValue(static_cast<double>(plugin_->quantizeSub.get()),
                                juce::dontSendNotification);
    velModeCombo_.setSelectedId(plugin_->velocityMode.get() + 1, juce::dontSendNotification);
    fixedVelSlider_.setValue(static_cast<double>(plugin_->fixedVelocity.get()),
                             juce::dontSendNotification);

    bool showFixed =
        static_cast<Arp::VelocityMode>(plugin_->velocityMode.get()) == Arp::VelocityMode::Fixed;
    fixedVelSlider_.setEnabled(showFixed);
    fixedVelSlider_.setAlpha(showFixed ? 1.0f : 0.3f);
}

void ArpeggiatorUI::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) {
    // Any property change — resync all values
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis)
            safeThis->syncFromPlugin();
    });
}

void ArpeggiatorUI::timerCallback() {
    if (!plugin_)
        return;

    // Read modulated values from AutomatableParams (includes macro modulation)
    float depth = plugin_->rampParam ? plugin_->rampParam->getCurrentValue() : plugin_->ramp.get();
    float skew = plugin_->skewParam ? plugin_->skewParam->getCurrentValue() : plugin_->skew.get();

    rampCurveDisplay_.setValues(depth, skew);
    depthSlider_.setValue(static_cast<double>(depth), juce::dontSendNotification);
    skewSlider_.setValue(static_cast<double>(skew), juce::dontSendNotification);

    // Playback sweep animation
    int step = plugin_->currentPlayStep_.load(std::memory_order_relaxed);
    int len = plugin_->currentSeqLength_.load(std::memory_order_relaxed);
    if (len > 0)
        rampCurveDisplay_.setNumTicks(len);
    int cycles = juce::jlimit(1, std::max(1, len), plugin_->rampCycles.get());
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

std::vector<LinkableTextSlider*> ArpeggiatorUI::getLinkableSliders() {
    // Pre-set param indices to match AutomatableParameter registration order:
    // 0=pattern, 1=rate, 2=octaves, 3=gate, 4=swing, 5=ramp, 6=skew, 7=latch, 8=velMode, 9=fixedVel
    // setupCustomUILinking() will use these indices (via getParamIndex()) instead of vector
    // position.
    magda::ChainNodePath dummy;
    rateSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 1, dummy);
    octavesSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 2, dummy);
    gateSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 3, dummy);
    swingSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 4, dummy);
    depthSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 5, dummy);
    skewSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 6, dummy);
    fixedVelSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 9, dummy);
    return {&rateSlider_,  &octavesSlider_, &gateSlider_,    &swingSlider_,
            &depthSlider_, &skewSlider_,    &fixedVelSlider_};
}

}  // namespace magda::daw::ui
