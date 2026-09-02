#include "custom_ui/StepSequencerUI.hpp"

#include "audio/sequencer/StepClock.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

using SeqPlugin = daw::audio::StepSequencerPlugin;

static const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

juce::String StepSequencerUI::noteNameShort(int noteNumber) {
    if (noteNumber < 0 || noteNumber > 127)
        return "-";
    int octave = (noteNumber / 12) - 2;
    return juce::String(NOTE_NAMES[noteNumber % 12]) + juce::String(octave);
}

// =============================================================================
// Construction
// =============================================================================

StepSequencerUI::StepSequencerUI() {
    setupLabel(rateLabel_, "RATE");
    setupSlider(rateSlider_, 0, 9, 1);
    static const char* rateNames[] = {"1/4.", "1/4",   "1/4T", "1/8.",  "1/8",
                                      "1/8T", "1/16.", "1/16", "1/16T", "1/32"};
    rateSlider_.setValueFormatter([](double v) {
        int idx = juce::jlimit(0, 9, juce::roundToInt(v));
        return juce::String(rateNames[idx]);
    });
    rateSlider_.setValueParser([](const juce::String&) { return 1.0; });
    rateSlider_.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kRate, static_cast<float>(juce::roundToInt(value)));
    };

    setupLabel(stepsLabel_, "STEPS");
    setupSlider(stepsSlider_, 1, SeqPlugin::MAX_STEPS, 1);
    stepsSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    stepsSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    stepsSlider_.onValueChanged = [this](double value) {
        const int steps = juce::jlimit(1, SeqPlugin::MAX_STEPS, juce::roundToInt(value));
        // How many steps play is part of the pattern, so it is a pattern edit.
        editPattern("Set Step Count", [steps](step_pattern::MonoPattern& p) { p.length = steps; });
        rampCurveDisplay_.setNumTicks(steps);
        // Clamp cycles to num steps
        cyclesSlider_.setRange(1.0, static_cast<double>(steps), 1.0);
        if (cyclesSlider_.getValue() > steps)
            cyclesSlider_.setValue(static_cast<double>(steps), juce::sendNotificationSync);
        repaint();
    };

    setupLabel(dirLabel_, "DIR");
    dirCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    dirCombo_.setColour(juce::ComboBox::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    dirCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    dirCombo_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    dirCombo_.addItem("Forward", 1);
    dirCombo_.addItem("Reverse", 2);
    dirCombo_.addItem("Ping-Pong", 3);
    dirCombo_.addItem("Random", 4);
    dirCombo_.onChange = [this] {
        sendChange(SeqPlugin::kDirection, static_cast<float>(dirCombo_.getSelectedId() - 1));
    };
    addAndMakeVisible(dirCombo_);

    setupLabel(swingLabel_, "SWING");
    setupSlider(swingSlider_, 0.0, 1.0, 0.01);
    swingSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    swingSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    swingSlider_.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kSwing, static_cast<float>(value));
    };

    setupLabel(glideLabel_, "GATE");
    setupSlider(glideSlider_, 0.05, 1.0, 0.01);
    glideSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    glideSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    glideSlider_.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kGateLength, static_cast<float>(value));
    };

    // --- Ramp curve (time warp) ---
    setupLabel(rampLabel_, "TIME BEND");
    addAndMakeVisible(rampCurveDisplay_);
    rampCurveDisplay_.onCurveChanged = [this](float depth, float skew) {
        sendChange(SeqPlugin::kRamp, depth);
        sendChange(SeqPlugin::kSkew, skew);
        depthSlider_.setValue(depth, juce::dontSendNotification);
        skewSlider_.setValue(skew, juce::dontSendNotification);
    };

    setupLabel(depthLabel_, "DEPTH");
    depthLabel_.setJustificationType(juce::Justification::centred);
    setupSlider(depthSlider_, -1.0, 1.0, 0.01);
    depthSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)); });
    depthSlider_.setValueParser(
        [](const juce::String& t) { return t.trim().getDoubleValue() / 100.0; });
    depthSlider_.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kRamp, static_cast<float>(value));
        rampCurveDisplay_.setValues(static_cast<float>(value),
                                    static_cast<float>(skewSlider_.getValue()));
    };

    setupLabel(skewLabel_, "SKEW");
    skewLabel_.setJustificationType(juce::Justification::centred);
    setupSlider(skewSlider_, -1.0, 1.0, 0.01);
    skewSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v * 100)); });
    skewSlider_.setValueParser(
        [](const juce::String& t) { return t.trim().getDoubleValue() / 100.0; });
    skewSlider_.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kSkew, static_cast<float>(value));
        rampCurveDisplay_.setValues(static_cast<float>(depthSlider_.getValue()),
                                    static_cast<float>(value));
    };

    setupLabel(cyclesLabel_, "CYCLES");
    setupSlider(cyclesSlider_, 1.0, static_cast<double>(SeqPlugin::MAX_STEPS), 1.0);
    cyclesSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    cyclesSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    cyclesSlider_.onValueChanged = [this](double) { settingsEdited(); };

    // Hard angle toggle (right-click on control point)
    rampCurveDisplay_.onHardAngleChanged = [this](bool hardAngle) {
        hardAngle_ = hardAngle;
        settingsEdited();
    };

    // Quantize slider (adaptive snap strength 0-100%)
    setupLabel(quantizeLabel_, "QUANTIZE");
    quantizeLabel_.setJustificationType(juce::Justification::centred);
    setupSlider(quantizeSlider_, 0.0, 1.0, 0.01);
    quantizeSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    quantizeSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    quantizeSlider_.onValueChanged = [this](double) { settingsEdited(); };

    // Quantize subdivisions (grid resolution, multiples of 16)
    setupLabel(quantizeSubLabel_, "SUB");
    setupSlider(quantizeSubSlider_, 16, 512, 16);
    quantizeSubSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)); });
    quantizeSubSlider_.setValueParser(
        [](const juce::String& t) { return t.trim().getDoubleValue(); });
    quantizeSubSlider_.onValueChanged = [this](double) { settingsEdited(); };

    // MIDI thru, step record and pattern randomize live in the device-slot
    // header (next to the AI button), owned by DeviceSlotComponent — not in
    // this body.
}

StepSequencerUI::~StepSequencerUI() {
    stopTimer();
    dirCombo_.setLookAndFeel(nullptr);
}

// =============================================================================
// Model and device binding
// =============================================================================

void StepSequencerUI::setSequencer(daw::audio::StepSequencerPlugin* device) {
    stopTimer();
    device_ = device;

    if (device_ != nullptr) {
        syncSettingsFromDevice();
        startTimerHz(30);
    }
}

void StepSequencerUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    const auto display = [&params](int index, float fallback) {
        return index < static_cast<int>(params.size())
                   ? params[static_cast<size_t>(index)].currentValue
                   : fallback;
    };

    rateSlider_.setValue(static_cast<double>(display(SeqPlugin::kRate, 7.0f)),
                         juce::dontSendNotification);
    dirCombo_.setSelectedId(juce::roundToInt(display(SeqPlugin::kDirection, 0.0f)) + 1,
                            juce::dontSendNotification);
    swingSlider_.setValue(static_cast<double>(display(SeqPlugin::kSwing, 0.0f)),
                          juce::dontSendNotification);
    glideSlider_.setValue(static_cast<double>(display(SeqPlugin::kGateLength, 0.8f)),
                          juce::dontSendNotification);

    const float depth = display(SeqPlugin::kRamp, 0.0f);
    const float skew = display(SeqPlugin::kSkew, 0.0f);
    depthSlider_.setValue(static_cast<double>(depth), juce::dontSendNotification);
    skewSlider_.setValue(static_cast<double>(skew), juce::dontSendNotification);
    rampCurveDisplay_.setValues(depth, skew);

    syncSettingsFromDevice();
    repaint();
}

void StepSequencerUI::setPattern(const step_pattern::MonoPattern& pattern) {
    if (pattern == pattern_)
        return;

    pattern_ = pattern;
    const int steps = pattern_.playingLength();
    stepsSlider_.setValue(static_cast<double>(steps), juce::dontSendNotification);
    rampCurveDisplay_.setNumTicks(steps);
    cyclesSlider_.setRange(1.0, static_cast<double>(steps), 1.0);
    repaint();
}

void StepSequencerUI::syncSettingsFromDevice() {
    if (device_ == nullptr)
        return;

    const int steps = pattern_.playingLength();
    hardAngle_ = device_->hardAngle.load(std::memory_order_relaxed);
    rampCurveDisplay_.setHardAngle(hardAngle_);
    quantizeSlider_.setValue(static_cast<double>(device_->quantize.load(std::memory_order_relaxed)),
                             juce::dontSendNotification);
    quantizeSubSlider_.setValue(
        static_cast<double>(device_->quantizeSub.load(std::memory_order_relaxed)),
        juce::dontSendNotification);
    cyclesSlider_.setValue(static_cast<double>(juce::jlimit(
                               1, steps, device_->rampCycles.load(std::memory_order_relaxed))),
                           juce::dontSendNotification);
}

void StepSequencerUI::sendChange(int paramIndex, float value) {
    if (onParameterChanged)
        onParameterChanged(paramIndex, value);
}

void StepSequencerUI::settingsEdited() {
    if (!onSettingsEdited)
        return;

    // The full settings set, from the controls, in the device's own state
    // vocabulary. The owner patches these onto the model's document; the device
    // itself is updated by the projection, not by this UI.
    using IDs = daw::audio::StepSequencerPlugin::SettingIDs;
    juce::NamedValueSet settings;
    settings.set(IDs::rampCycles, juce::roundToInt(cyclesSlider_.getValue()));
    settings.set(IDs::quantize, static_cast<float>(quantizeSlider_.getValue()));
    settings.set(IDs::quantizeSub, juce::roundToInt(quantizeSubSlider_.getValue()));
    settings.set(IDs::hardAngle, hardAngle_);
    onSettingsEdited(settings);
}

void StepSequencerUI::editPattern(const juce::String& description,
                                  std::function<void(step_pattern::MonoPattern&)> edit,
                                  magda::StepPatternGesture gesture) {
    if (!onPatternEdited)
        return;
    onPatternEdited(description, std::move(edit), gesture);
}

void StepSequencerUI::drainRecordedSteps() {
    if (device_ == nullptr)
        return;

    // The device heard the notes; the model is where they are written.
    daw::audio::StepSequencerPlugin::RecordedStep recorded;
    while (device_->popRecordedStep(recorded)) {
        editPattern("Record Step", [recorded](step_pattern::MonoPattern& p) {
            if (recorded.stepIndex < 0 || recorded.stepIndex >= daw::audio::sequencer::kMaxSteps)
                return;
            auto& step = p.steps[static_cast<size_t>(recorded.stepIndex)];
            step.noteNumber = juce::jlimit(0, 127, recorded.noteNumber);
            step.octaveShift = 0;
            step.gate = true;
        });
    }
}

void StepSequencerUI::timerCallback() {
    if (device_ == nullptr)
        return;

    drainRecordedSteps();

    // The device is the model's projection, so polling it catches every way a
    // pattern can change - this faceplate, an undo, an agent, a preset load -
    // without decoding the model's document 30 times a second. setPattern()
    // early-outs when nothing moved.
    setPattern(device_->pattern());

    const int numSteps = pattern_.playingLength();
    int step = device_->currentPlayStep_.load(std::memory_order_relaxed);
    bool needsRepaint = false;
    if (step != currentPlayStep_) {
        currentPlayStep_ = step;
        needsRepaint = true;
        // Update curve display sweep
        float pos = (step >= 0) ? static_cast<float>(step) / static_cast<float>(numSteps) : -1.0f;
        rampCurveDisplay_.setPlaybackPosition(
            pos, juce::jlimit(1, numSteps, device_->rampCycles.load(std::memory_order_relaxed)));
    }
    // Track step record position for highlight + parent header banner
    bool isRec = device_->isStepRecording();
    if (isRec) {
        int recPos = juce::jlimit(0, numSteps - 1,
                                  device_->stepRecordPosition_.load(std::memory_order_relaxed));
        if (recPos != selectedStep_) {
            selectedStep_ = recPos;
            needsRepaint = true;
            // Repaint parent so device header banner updates
            if (auto* parent = getParentComponent())
                parent->repaint();
        }
    }
    if (isRec != wasRecording_) {
        wasRecording_ = isRec;
        if (auto* parent = getParentComponent())
            parent->repaint();
    }
    if (needsRepaint)
        repaint();
}

// =============================================================================
// Layout
// =============================================================================

void StepSequencerUI::resized() {
    auto bounds = getLocalBounds().reduced(PADDING);

    // Controls row 1: RATE, STEPS, DIRECTION
    auto controlRow1 = bounds.removeFromTop(CONTROL_ROW_HEIGHT);
    int controlWidth = controlRow1.getWidth() / 3;
    {
        auto cell = controlRow1.removeFromLeft(controlWidth);
        rateLabel_.setBounds(cell.removeFromLeft(LABEL_WIDTH));
        rateSlider_.setBounds(cell);
    }
    {
        auto cell = controlRow1.removeFromLeft(controlWidth);
        stepsLabel_.setBounds(cell.removeFromLeft(LABEL_WIDTH));
        stepsSlider_.setBounds(cell);
    }
    {
        dirLabel_.setBounds(controlRow1.removeFromLeft(LABEL_WIDTH));
        dirCombo_.setBounds(controlRow1);
    }

    bounds.removeFromTop(ROW_GAP);

    // Controls row 2: SWING, GATE, Q, SUB, THRU, REC
    auto controlRow2 = bounds.removeFromTop(CONTROL_ROW_HEIGHT);
    // THRU / REC moved to the device-slot header. The whole row is controls.
    int quarterWidth = controlRow2.getWidth() / 4;
    {
        auto cell = controlRow2.removeFromLeft(quarterWidth);
        swingLabel_.setBounds(cell.removeFromLeft(LABEL_WIDTH));
        swingSlider_.setBounds(cell);
    }
    {
        auto cell = controlRow2.removeFromLeft(quarterWidth);
        glideLabel_.setBounds(cell.removeFromLeft(LABEL_WIDTH));
        glideSlider_.setBounds(cell);
    }
    {
        auto cell = controlRow2.removeFromLeft(quarterWidth);
        quantizeLabel_.setBounds(cell.removeFromLeft(LABEL_WIDTH));
        quantizeSlider_.setBounds(cell);
    }
    {
        quantizeSubLabel_.setBounds(controlRow2.removeFromLeft(LABEL_WIDTH));
        quantizeSubSlider_.setBounds(controlRow2);
    }

    bounds.removeFromTop(ROW_GAP + 2);

    // Mini timeline / step ruler (aligned to the step-box columns)
    timelineArea_ = bounds.removeFromTop(TIMELINE_HEIGHT).withTrimmedLeft(24);
    bounds.removeFromTop(ROW_GAP);

    // Step boxes (24px left margin to align with ACC/G/T label columns)
    stepBoxArea_ = bounds.removeFromTop(STEP_BOX_SIZE).withTrimmedLeft(24);
    bounds.removeFromTop(ROW_GAP);

    // Accent row
    accentArea_ = bounds.removeFromTop(TOGGLE_ROW_HEIGHT);
    bounds.removeFromTop(ROW_GAP);

    // Glide/Tie row (click = glide, shift+click = tie)
    glideTieArea_ = bounds.removeFromTop(TOGGLE_ROW_HEIGHT);
    bounds.removeFromTop(ROW_GAP + 2);

    // Keyboard with octave arrows on each side
    auto keyboardRow = bounds.removeFromTop(KEYBOARD_HEIGHT);
    octaveDownArea_ = keyboardRow.removeFromLeft(OCTAVE_ARROW_WIDTH);
    octaveUpArea_ = keyboardRow.removeFromRight(OCTAVE_ARROW_WIDTH);
    keyboardArea_ = keyboardRow;

    bounds.removeFromTop(ROW_GAP + 2);

    // TIME BEND label row
    constexpr int LABEL_H = 14;
    constexpr int CELL_H = CONTROL_ROW_HEIGHT + LABEL_H;
    constexpr int SLIDER_COL_W = 44;
    {
        auto labelRow = bounds.removeFromTop(LABEL_H);
        rampLabel_.setBounds(labelRow);
    }
    auto rampRow = bounds.removeFromTop(CELL_H * 3);
    {
        // Sliders stacked on the right, each with label above
        auto sliderCol = rampRow.removeFromRight(SLIDER_COL_W);
        auto depthCell = sliderCol.removeFromTop(CELL_H);
        depthLabel_.setBounds(depthCell.removeFromTop(LABEL_H));
        depthSlider_.setBounds(depthCell);
        auto skewCell = sliderCol.removeFromTop(CELL_H);
        skewLabel_.setBounds(skewCell.removeFromTop(LABEL_H));
        skewSlider_.setBounds(skewCell);
        auto cyclesCell = sliderCol.removeFromTop(CELL_H);
        cyclesLabel_.setBounds(cyclesCell.removeFromTop(LABEL_H));
        cyclesSlider_.setBounds(cyclesCell);
        // Curve fills remaining width
        rampRow.removeFromRight(4);
        rampCurveDisplay_.setBounds(rampRow);
    }
}

// =============================================================================
// Paint
// =============================================================================

void StepSequencerUI::paint(juce::Graphics& g) {
    drawTimeline(g, timelineArea_);
    drawStepBoxes(g, stepBoxArea_);
    drawAccentRow(g, accentArea_);
    drawGlideTieRow(g, glideTieArea_);
    drawOctaveArrow(g, octaveDownArea_, true);
    drawKeyboard(g, keyboardArea_);
    drawOctaveArrow(g, octaveUpArea_, false);
}

// Step ruler above the grid: a tick per step, heavier + numbered every 4, with
// the playing step highlighted. Columns align with the step boxes below.
void StepSequencerUI::drawTimeline(juce::Graphics& g, juce::Rectangle<int> area) {
    if (area.isEmpty())
        return;

    const int count = pattern_.playingLength();
    const float colW = static_cast<float>(area.getWidth()) / static_cast<float>(count);
    const float top = static_cast<float>(area.getY());
    const float bottom = static_cast<float>(area.getBottom());

    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.04f));
    g.fillRect(area);

    // Highlight the playing step.
    if (currentPlayStep_ >= 0 && currentPlayStep_ < count) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.45f));
        g.fillRect(
            juce::Rectangle<float>(area.getX() + currentPlayStep_ * colW, top, colW, bottom - top));
    }

    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    for (int i = 0; i < count; ++i) {
        const float x = area.getX() + i * colW;
        const bool group = (i % 4 == 0);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(group ? 0.5f : 0.2f));
        g.drawVerticalLine(juce::roundToInt(x), group ? top : top + 4.0f, bottom);
        if (group) {
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.drawText(
                juce::String(i + 1),
                juce::Rectangle<float>(x + 2.0f, top, colW - 2.0f, bottom - top).toNearestInt(),
                juce::Justification::centredLeft);
        }
    }

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f));
    g.drawHorizontalLine(area.getBottom() - 1, static_cast<float>(area.getX()),
                         static_cast<float>(area.getRight()));
}

void StepSequencerUI::drawStepBoxes(juce::Graphics& g, juce::Rectangle<int> area) {
    if (area.isEmpty())
        return;

    int count = pattern_.playingLength();
    float boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);
    auto font = FontManager::getInstance().getUIFont(8.0f);
    g.setFont(font);

    for (int i = 0; i < count; ++i) {
        const auto& step = pattern_.step(i);
        float x = area.getX() + i * boxW;
        auto boxRect = juce::Rectangle<float>(x + 0.5f, static_cast<float>(area.getY()),
                                              boxW - 1.0f, static_cast<float>(area.getHeight()));

        // Background
        juce::Colour bg = DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.08f);
        if (i == currentPlayStep_)
            bg = DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.3f);
        if (i == selectedStep_)
            bg = bg.brighter(0.15f);
        if (i == dragTargetStep_ && dragSourceStep_ >= 0 && i != dragSourceStep_)
            bg = DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).withAlpha(0.3f);
        if (!step.gate)
            bg = bg.darker(0.3f);

        g.setColour(bg);
        g.fillRoundedRectangle(boxRect, 2.0f);

        // Border
        juce::Colour border = DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f);
        if (i == selectedStep_)
            border = DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE);
        g.setColour(border);
        g.drawRoundedRectangle(boxRect, 2.0f, 0.5f);

        // Note name
        if (step.gate) {
            g.setColour(DarkTheme::getTextColour());
            g.drawText(noteNameShort(step.noteNumber + step.octaveShift * 12),
                       boxRect.toNearestInt(), juce::Justification::centred);
        }
    }
}

void StepSequencerUI::drawAccentRow(juce::Graphics& g, juce::Rectangle<int> area) {
    if (area.isEmpty())
        return;

    int count = pattern_.playingLength();
    float boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);
    auto font = FontManager::getInstance().getUIFont(7.0f);
    g.setFont(font);

    // Row label
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText("ACC", area.removeFromLeft(24), juce::Justification::centredLeft);

    float startX = static_cast<float>(area.getX());
    boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        const auto& step = pattern_.step(i);
        float x = startX + i * boxW;
        auto rect =
            juce::Rectangle<float>(x + 1.0f, static_cast<float>(area.getY()) + 1.0f, boxW - 2.0f,
                                   static_cast<float>(area.getHeight()) - 2.0f);

        if (step.accent) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ATTENTION).withAlpha(0.7f));
            g.fillRoundedRectangle(rect, 2.0f);
            g.setColour(DarkTheme::getTextColour());
            g.drawText("A", rect.toNearestInt(), juce::Justification::centred);
        } else {
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.2f));
            g.fillRoundedRectangle(rect, 2.0f);
        }
    }
}

void StepSequencerUI::drawGlideTieRow(juce::Graphics& g, juce::Rectangle<int> area) {
    if (area.isEmpty())
        return;

    int count = pattern_.playingLength();
    auto font = FontManager::getInstance().getUIFont(7.0f);
    g.setFont(font);

    // Row label
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText("G/T", area.removeFromLeft(24), juce::Justification::centredLeft);

    float boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        const auto& step = pattern_.step(i);
        float x = static_cast<float>(area.getX()) + i * boxW;
        auto rect =
            juce::Rectangle<float>(x + 1.0f, static_cast<float>(area.getY()) + 1.0f, boxW - 2.0f,
                                   static_cast<float>(area.getHeight()) - 2.0f);

        if (step.tie) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.7f));
            g.fillRoundedRectangle(rect, 2.0f);
            g.setColour(DarkTheme::getTextColour());
            g.drawText("T", rect.toNearestInt(), juce::Justification::centred);
        } else if (step.glide) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).withAlpha(0.7f));
            g.fillRoundedRectangle(rect, 2.0f);
            g.setColour(DarkTheme::getTextColour());
            g.drawText("~", rect.toNearestInt(), juce::Justification::centred);
        } else {
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.2f));
            g.fillRoundedRectangle(rect, 2.0f);
        }
    }
}

void StepSequencerUI::drawKeyboard(juce::Graphics& g, juce::Rectangle<int> area) {
    if (area.isEmpty())
        return;

    static const bool isBlack[] = {false, true,  false, true,  false, false,
                                   true,  false, true,  false, true,  false};
    static constexpr int WHITE_KEYS_PER_OCTAVE = 7;
    static constexpr int NUM_OCTAVES = 2;
    int totalWhiteKeys = WHITE_KEYS_PER_OCTAVE * NUM_OCTAVES;
    float whiteKeyW = static_cast<float>(area.getWidth()) / static_cast<float>(totalWhiteKeys);
    float whiteKeyH = static_cast<float>(area.getHeight());
    float blackKeyW = whiteKeyW * 0.65f;
    float blackKeyH = whiteKeyH * 0.6f;

    auto font = FontManager::getInstance().getUIFont(7.0f);
    g.setFont(font);

    // Get the selected step's resolved note (including octave shift) for highlighting
    const auto& selectedStepData = pattern_.step(selectedStep_);
    const int selectedNote = selectedStepData.noteNumber + (selectedStepData.octaveShift * 12);

    // Draw white keys
    int whiteIdx = 0;
    for (int note = 0; note < KEYBOARD_NUM_NOTES; ++note) {
        if (isBlack[note % 12])
            continue;

        float x = area.getX() + whiteIdx * whiteKeyW;
        auto keyRect =
            juce::Rectangle<float>(x, static_cast<float>(area.getY()), whiteKeyW - 1.0f, whiteKeyH);

        int midiNote = keyboardBaseNote_ + note;
        bool isSelected = (midiNote == selectedNote);

        g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.4f)
                               : juce::Colours::white.withAlpha(0.85f));
        g.fillRoundedRectangle(keyRect, 1.0f);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
        g.drawRoundedRectangle(keyRect, 1.0f, 0.5f);

        // Note label on C keys
        if (note % 12 == 0) {
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
            g.drawText(noteNameShort(midiNote),
                       keyRect.toNearestInt().withTrimmedTop(static_cast<int>(whiteKeyH * 0.6f)),
                       juce::Justification::centred);
        }
        ++whiteIdx;
    }

    // Draw black keys
    whiteIdx = 0;
    for (int note = 0; note < KEYBOARD_NUM_NOTES; ++note) {
        if (!isBlack[note % 12]) {
            ++whiteIdx;
            continue;
        }

        float x = area.getX() + (whiteIdx - 1) * whiteKeyW + whiteKeyW - blackKeyW / 2.0f;
        auto keyRect =
            juce::Rectangle<float>(x, static_cast<float>(area.getY()), blackKeyW, blackKeyH);

        int midiNote = keyboardBaseNote_ + note;
        bool isSelected = (midiNote == selectedNote);

        g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.6f)
                               : juce::Colours::black.withAlpha(0.85f));
        g.fillRoundedRectangle(keyRect, 1.0f);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.2f));
        g.drawRoundedRectangle(keyRect, 1.0f, 0.5f);
    }
}

void StepSequencerUI::drawOctaveArrow(juce::Graphics& g, juce::Rectangle<int> area, bool isLeft) {
    if (area.isEmpty())
        return;

    auto btn = area.reduced(2);
    bool canShift =
        isLeft ? (keyboardBaseNote_ > MIN_BASE_NOTE) : (keyboardBaseNote_ < MAX_BASE_NOTE);

    g.setColour(canShift ? DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.15f)
                         : DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRoundedRectangle(btn.toFloat(), 2.0f);

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
    g.drawRoundedRectangle(btn.toFloat(), 2.0f, 0.5f);

    // Draw arrow triangle
    g.setColour(canShift ? DarkTheme::getTextColour()
                         : DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
    float cx = btn.getCentreX();
    float cy = btn.getCentreY();
    float arrowSize = 5.0f;
    juce::Path arrow;
    if (isLeft) {
        arrow.addTriangle(cx + arrowSize, cy - arrowSize, cx + arrowSize, cy + arrowSize,
                          cx - arrowSize, cy);
    } else {
        arrow.addTriangle(cx - arrowSize, cy - arrowSize, cx - arrowSize, cy + arrowSize,
                          cx + arrowSize, cy);
    }
    g.fillPath(arrow);
}

// =============================================================================
// Mouse interaction
// =============================================================================

void StepSequencerUI::mouseDown(const juce::MouseEvent& e) {
    auto pos = e.getPosition();
    int count = pattern_.playingLength();

    // Step boxes — select step, toggle gate (right-click), or start shift+drag copy
    if (stepBoxArea_.contains(pos)) {
        int step = getStepAtX(pos.x, stepBoxArea_.getX(), stepBoxArea_.getWidth(), count);
        if (step >= 0 && step < count) {
            if (e.mods.isRightButtonDown()) {
                selectedStep_ = step;
                showStepContextMenu(step);
            } else if (e.mods.isShiftDown()) {
                dragSourceStep_ = step;
                dragTargetStep_ = step;
                selectedStep_ = step;
            } else {
                selectedStep_ = step;
            }
            repaint();
        }
        return;
    }

    // Accent row — toggle accent
    if (accentArea_.contains(pos)) {
        // Account for the label area
        auto contentArea = accentArea_.withTrimmedLeft(24);
        int step = getStepAtX(pos.x, contentArea.getX(), contentArea.getWidth(), count);
        if (step >= 0 && step < count) {
            editPattern("Toggle Step Accent", [step](step_pattern::MonoPattern& p) {
                auto& target = p.steps[static_cast<size_t>(step)];
                target.accent = !target.accent;
            });
            repaint();
        }
        return;
    }

    // Glide/Tie row — click = toggle glide, shift+click = toggle tie
    if (glideTieArea_.contains(pos)) {
        auto contentArea = glideTieArea_.withTrimmedLeft(24);
        int step = getStepAtX(pos.x, contentArea.getX(), contentArea.getWidth(), count);
        if (step >= 0 && step < count) {
            const bool tieGesture = e.mods.isShiftDown();
            editPattern(tieGesture ? "Toggle Step Tie" : "Toggle Step Glide",
                        [step, tieGesture](step_pattern::MonoPattern& p) {
                            auto& target = p.steps[static_cast<size_t>(step)];
                            // Tie and glide are mutually exclusive.
                            if (tieGesture) {
                                target.tie = !target.tie;
                                if (target.tie)
                                    target.glide = false;
                            } else {
                                target.glide = !target.glide;
                                if (target.glide)
                                    target.tie = false;
                            }
                        });
            repaint();
        }
        return;
    }

    // Octave down arrow
    if (octaveDownArea_.contains(pos)) {
        if (keyboardBaseNote_ > MIN_BASE_NOTE) {
            keyboardBaseNote_ = std::max(MIN_BASE_NOTE, keyboardBaseNote_ - 12);
            repaint();
        }
        return;
    }

    // Octave up arrow
    if (octaveUpArea_.contains(pos)) {
        if (keyboardBaseNote_ < MAX_BASE_NOTE) {
            keyboardBaseNote_ = std::min(MAX_BASE_NOTE, keyboardBaseNote_ + 12);
            repaint();
        }
        return;
    }

    // Keyboard — assign note to selected step
    if (keyboardArea_.contains(pos)) {
        int note = getKeyboardNoteAtPosition(pos, keyboardArea_);
        if (note >= 0 && note <= 127) {
            const int target = selectedStep_;
            editPattern("Set Step Note", [target, note](step_pattern::MonoPattern& p) {
                auto& step = p.steps[static_cast<size_t>(target)];
                step.noteNumber = note;
                step.octaveShift = 0;
            });
            repaint();
        }
        return;
    }
}

void StepSequencerUI::mouseDrag(const juce::MouseEvent& e) {
    if (dragSourceStep_ < 0)
        return;

    auto pos = e.getPosition();
    int count = pattern_.playingLength();

    if (stepBoxArea_.contains(pos)) {
        int step = getStepAtX(pos.x, stepBoxArea_.getX(), stepBoxArea_.getWidth(), count);
        if (step >= 0 && step < count && step != dragTargetStep_) {
            dragTargetStep_ = step;
            repaint();
        }
    }
}

void StepSequencerUI::mouseUp(const juce::MouseEvent&) {
    if (dragSourceStep_ < 0) {
        dragSourceStep_ = -1;
        dragTargetStep_ = -1;
        return;
    }

    int count = pattern_.playingLength();

    // Copy source step to target if they differ
    if (dragTargetStep_ >= 0 && dragTargetStep_ < count && dragTargetStep_ != dragSourceStep_) {
        const int from = dragSourceStep_;
        const int to = dragTargetStep_;
        editPattern("Copy Step", [from, to](step_pattern::MonoPattern& p) {
            p.steps[static_cast<size_t>(to)] = p.steps[static_cast<size_t>(from)];
        });
        selectedStep_ = dragTargetStep_;
    }

    dragSourceStep_ = -1;
    dragTargetStep_ = -1;
    repaint();
}

// =============================================================================
// Hit testing helpers
// =============================================================================

int StepSequencerUI::getStepAtX(int x, int areaX, int areaWidth, int numSteps) const {
    if (numSteps <= 0 || areaWidth <= 0)
        return -1;
    int relX = x - areaX;
    if (relX < 0 || relX >= areaWidth)
        return -1;
    return relX * numSteps / areaWidth;
}

int StepSequencerUI::getKeyboardNoteAtPosition(juce::Point<int> pos,
                                               juce::Rectangle<int> area) const {
    // Check black keys first (they overlay white keys)
    static const bool isBlack[] = {false, true,  false, true,  false, false,
                                   true,  false, true,  false, true,  false};
    static constexpr int WHITE_KEYS_PER_OCTAVE = 7;
    static constexpr int NUM_OCTAVES = 2;
    int totalWhiteKeys = WHITE_KEYS_PER_OCTAVE * NUM_OCTAVES;
    float whiteKeyW = static_cast<float>(area.getWidth()) / static_cast<float>(totalWhiteKeys);
    float blackKeyW = whiteKeyW * 0.65f;
    float blackKeyH = static_cast<float>(area.getHeight()) * 0.6f;

    // Check black keys
    int whiteIdx = 0;
    for (int note = 0; note < KEYBOARD_NUM_NOTES; ++note) {
        if (!isBlack[note % 12]) {
            ++whiteIdx;
            continue;
        }

        float x = area.getX() + (whiteIdx - 1) * whiteKeyW + whiteKeyW - blackKeyW / 2.0f;
        auto keyRect =
            juce::Rectangle<float>(x, static_cast<float>(area.getY()), blackKeyW, blackKeyH);
        if (keyRect.contains(pos.toFloat()))
            return keyboardBaseNote_ + note;
    }

    // Check white keys
    whiteIdx = 0;
    for (int note = 0; note < KEYBOARD_NUM_NOTES; ++note) {
        if (isBlack[note % 12])
            continue;

        float x = area.getX() + whiteIdx * whiteKeyW;
        auto keyRect = juce::Rectangle<float>(x, static_cast<float>(area.getY()), whiteKeyW,
                                              static_cast<float>(area.getHeight()));
        if (keyRect.contains(pos.toFloat()))
            return keyboardBaseNote_ + note;
        ++whiteIdx;
    }

    return -1;
}

// =============================================================================
// Context menu
// =============================================================================

void StepSequencerUI::showStepContextMenu(int stepIndex) {
    int count = pattern_.playingLength();
    if (stepIndex < 0 || stepIndex >= count)
        return;

    const auto& step = pattern_.step(stepIndex);

    juce::PopupMenu menu;
    menu.addItem(1, step.gate ? "Mute Step" : "Unmute Step");
    menu.addItem(2, "Copy to All Steps");
    menu.addItem(3, "Clear Step");
    menu.addSeparator();
    menu.addItem(4, "Clear Pattern");

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis = juce::Component::SafePointer(this),
                                                    stepIndex, count](int result) {
        if (safeThis == nullptr)
            return;
        switch (result) {
            case 1:
                safeThis->editPattern("Mute Step", [stepIndex](step_pattern::MonoPattern& p) {
                    auto& target = p.steps[static_cast<size_t>(stepIndex)];
                    target.gate = !target.gate;
                });
                break;
            case 2:
                safeThis->editPattern(
                    "Copy Step to All", [stepIndex, count](step_pattern::MonoPattern& p) {
                        const auto source = p.steps[static_cast<size_t>(stepIndex)];
                        for (int i = 0; i < count; ++i)
                            p.steps[static_cast<size_t>(i)] = source;
                    });
                break;
            case 3:
                safeThis->editPattern("Clear Step", [stepIndex](step_pattern::MonoPattern& p) {
                    p.steps[static_cast<size_t>(stepIndex)] = {};
                });
                break;
            case 4:
                safeThis->editPattern("Clear Pattern",
                                      [](step_pattern::MonoPattern& p) { p.steps.fill({}); });
                break;
            default:
                return;
        }
        safeThis->repaint();
    });
}

// =============================================================================
// Setup helpers
// =============================================================================

void StepSequencerUI::setupLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
}

void StepSequencerUI::setupSlider(LinkableTextSlider& slider, double min, double max, double step) {
    slider.setRange(min, max, step);
    addAndMakeVisible(slider);
}

void StepSequencerUI::lookAndFeelChanged() {
    // Re-apply cached theme colours after a live theme switch.
    for (auto* label :
         {&rateLabel_, &stepsLabel_, &dirLabel_, &swingLabel_, &glideLabel_, &rampLabel_,
          &depthLabel_, &skewLabel_, &cyclesLabel_, &quantizeLabel_, &quantizeSubLabel_})
        label->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());

    dirCombo_.setColour(juce::ComboBox::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    dirCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    dirCombo_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));

    repaint();
}

std::vector<LinkableTextSlider*> StepSequencerUI::getLinkableSliders() {
    magda::ChainNodePath dummy;
    // Param indices match AutomatableParameter registration order:
    // 0=rate, 1=direction, 2=swing, 3=glidetime, 4=accentvel, 5=normalvel, 6=ramp, 7=skew
    rateSlider_.setLinkContext(magda::INVALID_DEVICE_ID, SeqPlugin::kRate, dummy);
    swingSlider_.setLinkContext(magda::INVALID_DEVICE_ID, SeqPlugin::kSwing, dummy);
    glideSlider_.setLinkContext(magda::INVALID_DEVICE_ID, SeqPlugin::kGateLength, dummy);
    depthSlider_.setLinkContext(magda::INVALID_DEVICE_ID, SeqPlugin::kRamp, dummy);
    skewSlider_.setLinkContext(magda::INVALID_DEVICE_ID, SeqPlugin::kSkew, dummy);
    return {&rateSlider_, &swingSlider_, &glideSlider_, &depthSlider_, &skewSlider_};
}

}  // namespace magda::daw::ui
