#include "ArpeggiatorUI.hpp"

#include "ui/themes/SmallButtonLookAndFeel.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

using Arp = daw::audio::ArpeggiatorPlugin;

// (LinkableTextSlider replaces the old ArpSliderLookAndFeel)

void RampCurveDisplay::paint(juce::Graphics& g) {
    auto outerBounds = getLocalBounds().toFloat();
    if (outerBounds.getWidth() < 8.0f || outerBounds.getHeight() < 8.0f)
        return;

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.08f));
    g.fillRoundedRectangle(outerBounds, 2.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
    g.drawRoundedRectangle(outerBounds.reduced(0.5f), 2.0f, 0.5f);

    // Inset for curve content (padding inside the border)
    auto bounds = outerBounds.reduced(8.0f);
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float x0 = bounds.getX();
    float y0 = bounds.getY();

    // Grid lines (4x4)
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f));
    for (int i = 1; i < 4; ++i) {
        float fx = x0 + w * (static_cast<float>(i) / 4.0f);
        float fy = y0 + h * (static_cast<float>(i) / 4.0f);
        g.drawLine(fx, y0, fx, y0 + h, 1.0f);
        g.drawLine(x0, fy, x0 + w, fy, 1.0f);
    }

    // Diagonal reference line (linear)
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
    g.drawLine(x0, y0 + h, x0 + w, y0, 0.5f);

    // Draw the curve
    juce::Path curvePath;
    constexpr int NUM_POINTS = 48;
    for (int i = 0; i <= NUM_POINTS; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(NUM_POINTS);
        double curved = daw::audio::ArpeggiatorPlugin::applyRampCurve(t, depth_, skew_);
        float px = x0 + static_cast<float>(t) * w;
        float py = y0 + h - static_cast<float>(curved) * h;
        if (i == 0)
            curvePath.startNewSubPath(px, py);
        else
            curvePath.lineTo(px, py);
    }
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    g.strokePath(curvePath, juce::PathStrokeType(1.5f));

    // Handle circle at the bezier control point: (s, s+depth) in graph space.
    // Remap skew from [-1,1] to [0.01,0.99] for display position.
    float s = 0.5f + skew_ * 0.49f;
    float hx = x0 + s * w;
    float hy = y0 + h - (s + depth_) * h;
    // Clamp to visible area so the handle never escapes the component
    hx = juce::jlimit(x0, x0 + w, hx);
    hy = juce::jlimit(y0, y0 + h, hy);

    constexpr float HANDLE_R = 4.0f;
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillEllipse(hx - HANDLE_R, hy - HANDLE_R, HANDLE_R * 2.0f, HANDLE_R * 2.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    g.drawEllipse(hx - HANDLE_R, hy - HANDLE_R, HANDLE_R * 2.0f, HANDLE_R * 2.0f, 1.5f);
}

void RampCurveDisplay::mouseDown(const juce::MouseEvent& e) {
    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float x0 = bounds.getX();
    float y0 = bounds.getY();
    // Record offset from click point to handle centre so the handle doesn't jump
    float s = 0.5f + skew_ * 0.49f;
    float handleX = x0 + s * w;
    float handleY = y0 + h - (s + depth_) * h;
    handleOffsetX_ = handleX - e.position.x;
    handleOffsetY_ = handleY - e.position.y;
}

void RampCurveDisplay::mouseDrag(const juce::MouseEvent& e) {
    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float x0 = bounds.getX();
    float y0 = bounds.getY();

    // Translate mouse position to where the handle centre should be
    float cx = e.position.x + handleOffsetX_;
    float cy = e.position.y + handleOffsetY_;

    // Control point (s, s+depth) in graph space maps to screen as:
    //   cx = x0 + s * w   →   s = (cx - x0) / w
    //   cy = y0 + h - (s + depth) * h   →   depth = (y0 + h - cy) / h - s
    // Then remap s from [0.01,0.99] back to skew [-1,1]: skew = (s - 0.5) / 0.49
    float sRaw = (cx - x0) / w;
    float s = juce::jlimit(0.01f, 0.99f, sRaw);
    float newDepth = (y0 + h - cy) / h - s;
    float newSkew = (s - 0.5f) / 0.49f;

    newSkew = juce::jlimit(-1.0f, 1.0f, newSkew);
    newDepth = juce::jlimit(-1.0f, 1.0f, newDepth);

    depth_ = newDepth;
    skew_ = newSkew;
    repaint();
    if (onCurveChanged)
        onCurveChanged(newDepth, newSkew);
}

void RampCurveDisplay::mouseDoubleClick(const juce::MouseEvent&) {
    depth_ = 0.0f;
    skew_ = 0.0f;
    repaint();
    if (onCurveChanged)
        onCurveChanged(0.0f, 0.0f);
}

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
    setupLabel(rampLabel_, "TIMING");
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
    setupLabel(depthLabel_, "Y");
    setupSlider(depthSlider_, -1.0, 1.0, 0.01);
    depthSlider_.setValueFormatter([](double v) { return juce::String(v, 2); });
    depthSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    depthSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->ramp = static_cast<float>(value);
        rampCurveDisplay_.setValues(static_cast<float>(value), rampCurveDisplay_.getSkew());
    };

    setupLabel(skewLabel_, "X");
    setupSlider(skewSlider_, -1.0, 1.0, 0.01);
    skewSlider_.setValueFormatter([](double v) { return juce::String(v, 2); });
    skewSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    skewSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->skew = static_cast<float>(value);
        rampCurveDisplay_.setValues(rampCurveDisplay_.getDepth(), static_cast<float>(value));
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

    // Timing curve: full-width below both columns with side padding
    bounds.removeFromTop(ROW_GAP);
    int xyRowHeight = ROW_HEIGHT + ROW_GAP;
    if (bounds.getHeight() > ROW_HEIGHT + ROW_GAP + xyRowHeight + 4) {
        rampLabel_.setBounds(bounds.removeFromTop(ROW_HEIGHT));
        bounds.removeFromTop(ROW_GAP);

        // Reserve bottom row for X/Y sliders
        auto xyRow = bounds.removeFromBottom(ROW_HEIGHT);
        bounds.removeFromBottom(ROW_GAP);
        rampCurveDisplay_.setBounds(bounds);

        // X/Y sliders side by side
        int xyLabelW = 14;
        int halfW = xyRow.getWidth() / 2;
        auto leftXY = xyRow.removeFromLeft(halfW).reduced(2, 0);
        auto rightXY = xyRow.reduced(2, 0);
        skewLabel_.setBounds(leftXY.removeFromLeft(xyLabelW));
        skewSlider_.setBounds(leftXY);
        depthLabel_.setBounds(rightXY.removeFromLeft(xyLabelW));
        depthSlider_.setBounds(rightXY);
    }
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
