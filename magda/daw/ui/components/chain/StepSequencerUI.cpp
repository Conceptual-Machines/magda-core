#include "StepSequencerUI.hpp"

#include <juce_llm/juce_llm.h>

#include "../../../../agents/llm_client_factory.hpp"
#include "audio/StepClock.hpp"
#include "core/Config.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"
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
        if (plugin_)
            plugin_->rate = juce::roundToInt(value);
    };

    setupLabel(stepsLabel_, "STEPS");
    setupSlider(stepsSlider_, 1, SeqPlugin::MAX_STEPS, 1);
    stepsSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    stepsSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    stepsSlider_.onValueChanged = [this](double value) {
        if (plugin_) {
            plugin_->numSteps = juce::roundToInt(value);
            repaint();
        }
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
        if (plugin_)
            plugin_->direction = dirCombo_.getSelectedId() - 1;
    };
    addAndMakeVisible(dirCombo_);

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

    setupLabel(glideLabel_, "GATE");
    setupSlider(glideSlider_, 0.05, 1.0, 0.01);
    glideSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    glideSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    glideSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->gateLength = static_cast<float>(value);
    };

    // --- Ramp curve (time warp) ---
    setupLabel(rampLabel_, "RAMP");
    addAndMakeVisible(rampCurveDisplay_);
    rampCurveDisplay_.onCurveChanged = [this](float depth, float skew) {
        if (plugin_) {
            plugin_->ramp = depth;
            plugin_->skew = skew;
        }
    };

    setupLabel(depthLabel_, "DEPTH");
    setupSlider(depthSlider_, -1.0, 1.0, 0.01);
    depthSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)); });
    depthSlider_.setValueParser(
        [](const juce::String& t) { return t.trim().getDoubleValue() / 100.0; });
    depthSlider_.onValueChanged = [this](double value) {
        if (plugin_) {
            plugin_->ramp = static_cast<float>(value);
            rampCurveDisplay_.setValues(static_cast<float>(value), plugin_->skew.get());
        }
    };

    setupLabel(skewLabel_, "SKEW");
    setupSlider(skewSlider_, -1.0, 1.0, 0.01);
    skewSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v * 100)); });
    skewSlider_.setValueParser(
        [](const juce::String& t) { return t.trim().getDoubleValue() / 100.0; });
    skewSlider_.onValueChanged = [this](double value) {
        if (plugin_) {
            plugin_->skew = static_cast<float>(value);
            rampCurveDisplay_.setValues(plugin_->ramp.get(), static_cast<float>(value));
        }
    };

    // --- Pattern generation: [RND] [prompt input] [Generate] ---
    randomButton_ = std::make_unique<magda::SvgButton>("Random", BinaryData::random_svg,
                                                       BinaryData::random_svgSize);
    randomButton_->setTooltip("Randomize pattern");
    randomButton_->onClick = [this] {
        if (plugin_) {
            plugin_->randomizePattern();
            repaint();
        }
    };
    addAndMakeVisible(randomButton_.get());

    aiPromptEditor_.setMultiLine(false);
    aiPromptEditor_.setTextToShowWhenEmpty("describe a pattern...",
                                           DarkTheme::getColour(DarkTheme::TEXT_DIM));
    aiPromptEditor_.setColour(juce::TextEditor::backgroundColourId,
                              DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    aiPromptEditor_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    aiPromptEditor_.setColour(juce::TextEditor::outlineColourId,
                              DarkTheme::getColour(DarkTheme::BORDER));
    aiPromptEditor_.setColour(juce::TextEditor::focusedOutlineColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    aiPromptEditor_.setFont(FontManager::getInstance().getUIFont(9.0f));
    aiPromptEditor_.onReturnKey = [this] { generateAIPattern(); };
    addAndMakeVisible(aiPromptEditor_);

    aiButton_.setButtonText("Generate");
    aiButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    aiButton_.setColour(juce::TextButton::buttonColourId,
                        DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    aiButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    aiButton_.setTooltip("Generate pattern with AI");
    aiButton_.onClick = [this] { generateAIPattern(); };
    addAndMakeVisible(aiButton_);

    aiStatusLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    aiStatusLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    aiStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(aiStatusLabel_);
}

StepSequencerUI::~StepSequencerUI() {
    stopTimer();
    if (watchedState_.isValid())
        watchedState_.removeListener(this);
    dirCombo_.setLookAndFeel(nullptr);
    aiButton_.setLookAndFeel(nullptr);
}

// =============================================================================
// Plugin binding
// =============================================================================

void StepSequencerUI::setPlugin(daw::audio::StepSequencerPlugin* plugin) {
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

void StepSequencerUI::syncFromPlugin() {
    if (!plugin_)
        return;

    rateSlider_.setValue(static_cast<double>(plugin_->rate.get()), juce::dontSendNotification);
    stepsSlider_.setValue(static_cast<double>(plugin_->numSteps.get()), juce::dontSendNotification);
    dirCombo_.setSelectedId(plugin_->direction.get() + 1, juce::dontSendNotification);
    swingSlider_.setValue(static_cast<double>(plugin_->swing.get()), juce::dontSendNotification);
    glideSlider_.setValue(static_cast<double>(plugin_->gateLength.get()),
                          juce::dontSendNotification);
    depthSlider_.setValue(static_cast<double>(plugin_->ramp.get()), juce::dontSendNotification);
    skewSlider_.setValue(static_cast<double>(plugin_->skew.get()), juce::dontSendNotification);
    rampCurveDisplay_.setValues(plugin_->ramp.get(), plugin_->skew.get());
    repaint();
}

void StepSequencerUI::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) {
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis)
            safeThis->syncFromPlugin();
    });
}

void StepSequencerUI::timerCallback() {
    if (!plugin_)
        return;

    int step = plugin_->currentPlayStep_.load(std::memory_order_relaxed);
    if (step != currentPlayStep_) {
        currentPlayStep_ = step;
        repaint();  // Repaint to update step highlight
    }
}

// =============================================================================
// Layout
// =============================================================================

void StepSequencerUI::resized() {
    auto bounds = getLocalBounds().reduced(PADDING);

    // Controls row (two rows of label+control pairs)
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
    auto controlRow2 = bounds.removeFromTop(CONTROL_ROW_HEIGHT);
    int halfWidth = controlRow2.getWidth() / 2;
    {
        auto cell = controlRow2.removeFromLeft(halfWidth);
        swingLabel_.setBounds(cell.removeFromLeft(LABEL_WIDTH));
        swingSlider_.setBounds(cell);
    }
    {
        glideLabel_.setBounds(controlRow2.removeFromLeft(LABEL_WIDTH));
        glideSlider_.setBounds(controlRow2);
    }

    bounds.removeFromTop(ROW_GAP + 2);

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

    // Ramp curve section: [RAMP label] [curve display] [DEPTH slider] [SKEW slider]
    auto rampRow = bounds.removeFromTop(CONTROL_ROW_HEIGHT * 2);
    {
        rampLabel_.setBounds(rampRow.removeFromLeft(LABEL_WIDTH));
        rampCurveDisplay_.setBounds(rampRow.removeFromLeft(rampRow.getHeight()));
        rampRow.removeFromLeft(4);
        int sliderWidth = rampRow.getWidth() / 2;
        auto depthCell = rampRow.removeFromLeft(sliderWidth);
        depthLabel_.setBounds(depthCell.removeFromLeft(LABEL_WIDTH));
        depthSlider_.setBounds(depthCell);
        skewLabel_.setBounds(rampRow.removeFromLeft(LABEL_WIDTH));
        skewSlider_.setBounds(rampRow);
    }

    bounds.removeFromTop(ROW_GAP);

    // Pattern generation row: [RND] [prompt editor] [Generate]
    auto buttonRow = bounds.removeFromTop(CONTROL_ROW_HEIGHT);
    randomButton_->setBounds(buttonRow.removeFromLeft(CONTROL_ROW_HEIGHT));
    buttonRow.removeFromLeft(4);
    aiButton_.setBounds(buttonRow.removeFromRight(60));
    buttonRow.removeFromRight(4);
    aiPromptEditor_.setBounds(buttonRow);

    // AI status row (streaming response / result description)
    bounds.removeFromTop(2);
    aiStatusLabel_.setBounds(bounds.removeFromTop(CONTROL_ROW_HEIGHT));
}

// =============================================================================
// Paint
// =============================================================================

void StepSequencerUI::paint(juce::Graphics& g) {
    drawStepBoxes(g, stepBoxArea_);
    drawAccentRow(g, accentArea_);
    drawGlideTieRow(g, glideTieArea_);
    drawOctaveArrow(g, octaveDownArea_, true);
    drawKeyboard(g, keyboardArea_);
    drawOctaveArrow(g, octaveUpArea_, false);
}

void StepSequencerUI::drawStepBoxes(juce::Graphics& g, juce::Rectangle<int> area) {
    if (!plugin_ || area.isEmpty())
        return;

    int count = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());
    float boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);
    auto font = FontManager::getInstance().getUIFont(8.0f);
    g.setFont(font);

    for (int i = 0; i < count; ++i) {
        auto step = plugin_->getStep(i);
        float x = area.getX() + i * boxW;
        auto boxRect = juce::Rectangle<float>(x + 0.5f, static_cast<float>(area.getY()),
                                              boxW - 1.0f, static_cast<float>(area.getHeight()));

        // Background
        juce::Colour bg = DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.08f);
        if (i == currentPlayStep_)
            bg = DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.3f);
        if (i == selectedStep_)
            bg = bg.brighter(0.15f);
        if (i == dragTargetStep_ && dragSourceStep_ >= 0 && i != dragSourceStep_)
            bg = DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f);
        if (!step.gate)
            bg = bg.darker(0.3f);

        g.setColour(bg);
        g.fillRoundedRectangle(boxRect, 2.0f);

        // Border
        juce::Colour border = DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f);
        if (i == selectedStep_)
            border = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);
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
    if (!plugin_ || area.isEmpty())
        return;

    int count = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());
    float boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);
    auto font = FontManager::getInstance().getUIFont(7.0f);
    g.setFont(font);

    // Row label
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText("ACC", area.removeFromLeft(24), juce::Justification::centredLeft);

    float startX = static_cast<float>(area.getX());
    boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        auto step = plugin_->getStep(i);
        float x = startX + i * boxW;
        auto rect =
            juce::Rectangle<float>(x + 1.0f, static_cast<float>(area.getY()) + 1.0f, boxW - 2.0f,
                                   static_cast<float>(area.getHeight()) - 2.0f);

        if (step.accent) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.7f));
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
    if (!plugin_ || area.isEmpty())
        return;

    int count = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());
    auto font = FontManager::getInstance().getUIFont(7.0f);
    g.setFont(font);

    // Row label
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText("G/T", area.removeFromLeft(24), juce::Justification::centredLeft);

    float boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        auto step = plugin_->getStep(i);
        float x = static_cast<float>(area.getX()) + i * boxW;
        auto rect =
            juce::Rectangle<float>(x + 1.0f, static_cast<float>(area.getY()) + 1.0f, boxW - 2.0f,
                                   static_cast<float>(area.getHeight()) - 2.0f);

        if (step.tie) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.7f));
            g.fillRoundedRectangle(rect, 2.0f);
            g.setColour(DarkTheme::getTextColour());
            g.drawText("T", rect.toNearestInt(), juce::Justification::centred);
        } else if (step.glide) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.7f));
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
    int selectedNote = -1;
    if (plugin_) {
        auto step = plugin_->getStep(selectedStep_);
        selectedNote = step.noteNumber + step.octaveShift * 12;
    }

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

        g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.4f)
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

        g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.6f)
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
    if (!plugin_)
        return;
    auto pos = e.getPosition();
    int count = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());

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
            auto s = plugin_->getStep(step);
            plugin_->setStepAccent(step, !s.accent);
            repaint();
        }
        return;
    }

    // Glide/Tie row — click = toggle glide, shift+click = toggle tie
    if (glideTieArea_.contains(pos)) {
        auto contentArea = glideTieArea_.withTrimmedLeft(24);
        int step = getStepAtX(pos.x, contentArea.getX(), contentArea.getWidth(), count);
        if (step >= 0 && step < count) {
            auto s = plugin_->getStep(step);
            if (e.mods.isShiftDown()) {
                plugin_->setStepTie(step, !s.tie);
                if (!s.tie)
                    plugin_->setStepGlide(step, false);  // Tie and glide are mutually exclusive
            } else {
                plugin_->setStepGlide(step, !s.glide);
                if (!s.glide)
                    plugin_->setStepTie(step, false);  // Tie and glide are mutually exclusive
            }
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
            plugin_->setStepNote(selectedStep_, note);
            plugin_->setStepOctaveShift(selectedStep_, 0);
            repaint();
        }
        return;
    }
}

void StepSequencerUI::mouseDrag(const juce::MouseEvent& e) {
    if (!plugin_ || dragSourceStep_ < 0)
        return;

    auto pos = e.getPosition();
    int count = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());

    if (stepBoxArea_.contains(pos)) {
        int step = getStepAtX(pos.x, stepBoxArea_.getX(), stepBoxArea_.getWidth(), count);
        if (step >= 0 && step < count && step != dragTargetStep_) {
            dragTargetStep_ = step;
            repaint();
        }
    }
}

void StepSequencerUI::mouseUp(const juce::MouseEvent&) {
    if (!plugin_ || dragSourceStep_ < 0) {
        dragSourceStep_ = -1;
        dragTargetStep_ = -1;
        return;
    }

    int count = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());

    // Copy source step to target if they differ
    if (dragTargetStep_ >= 0 && dragTargetStep_ < count && dragTargetStep_ != dragSourceStep_) {
        auto src = plugin_->getStep(dragSourceStep_);
        plugin_->setStepNote(dragTargetStep_, src.noteNumber);
        plugin_->setStepOctaveShift(dragTargetStep_, src.octaveShift);
        plugin_->setStepGate(dragTargetStep_, src.gate);
        plugin_->setStepAccent(dragTargetStep_, src.accent);
        plugin_->setStepGlide(dragTargetStep_, src.glide);
        plugin_->setStepTie(dragTargetStep_, src.tie);
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
// AI pattern generation
// =============================================================================

void StepSequencerUI::generateAIPattern() {
    if (!plugin_)
        return;

    auto prompt = aiPromptEditor_.getText().trim();
    if (prompt.isEmpty())
        return;

    int numSteps = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());

    // Build JSON schema for structured output
    auto stepSchema = llm::Schema::object({
        {"note", llm::Schema::integer()},
        {"octave", llm::Schema::integer()},
        {"gate", llm::Schema::boolean()},
        {"accent", llm::Schema::boolean()},
        {"glide", llm::Schema::boolean()},
        {"tie", llm::Schema::boolean()},
    });
    auto stepsArraySchema = llm::Schema::array(stepSchema);
    if (auto* obj = stepsArraySchema.getDynamicObject()) {
        obj->setProperty("minItems", numSteps);
        obj->setProperty("maxItems", numSteps);
    }
    auto responseSchema = llm::Schema::object({
        {"description", llm::Schema::string()},
        {"steps", stepsArraySchema},
    });

    auto systemPrompt =
        juce::String("You are a music pattern generator for a 303-style step sequencer.\n"
                     "Generate a JSON object with:\n"
                     "  - description: a short label for the pattern (e.g. \"Acid C minor\", "
                     "\"Funky bassline\")\n"
                     "  - steps: array of step objects\n"
                     "Each step has:\n"
                     "  - note: MIDI note number (0-127, e.g. 60=C4, 48=C3, 36=C2)\n"
                     "  - octave: octave shift (-2 to +2, applied on top of note)\n"
                     "  - gate: true if the step plays, false for rest\n"
                     "  - accent: true for accented (louder) steps\n"
                     "  - glide: true for portamento slide to next note\n"
                     "  - tie: true to extend previous note without retriggering\n"
                     "Generate exactly " +
                     juce::String(numSteps) +
                     " steps. Return only 1 pattern.\n"
                     "Keep octave at 0 unless the user asks for wide range.\n"
                     "For acid bass lines, use lots of glides and accents on root/fifth notes.");

    // Disable controls and show thinking indicator
    aiButton_.setEnabled(false);
    aiButton_.setButtonText("...");
    aiPromptEditor_.setEnabled(false);
    aiStatusLabel_.setText("Thinking...", juce::dontSendNotification);

    auto safeThis = juce::Component::SafePointer<StepSequencerUI>(this);

    // Run LLM call on background thread with streaming
    std::thread([safeThis, prompt, systemPrompt, responseSchema, numSteps]() {
        auto agentConfig = magda::Config::getInstance().getAgentLLMConfig("music");
        auto client = magda::createLLMClient(agentConfig, "music");

        llm::Request request;
        request.systemPrompt = systemPrompt;
        request.userMessage = prompt;
        request.temperature = 0.7f;
        request.schema = responseSchema;

        DBG("AI pattern request - user: " + prompt);

        // Stream tokens to the status label for live feedback
        auto response = client->sendStreamingRequest(request, [&](const juce::String& token) {
            auto tokenCopy = token;
            juce::MessageManager::callAsync([safeThis, tokenCopy]() {
                if (!safeThis)
                    return;
                auto current = safeThis->aiStatusLabel_.getText();
                if (current == "Thinking...")
                    current = "";
                safeThis->aiStatusLabel_.setText(current + tokenCopy, juce::dontSendNotification);
            });
            return true;
        });

        DBG("AI pattern response - success: " + juce::String(response.success ? "true" : "false"));
        if (!response.success)
            DBG("AI pattern response - error: " + response.error);
        DBG("AI pattern response - text: " + response.text);

        juce::MessageManager::callAsync([safeThis, response, prompt, numSteps]() {
            if (!safeThis)
                return;

            // Re-enable controls
            safeThis->aiButton_.setEnabled(true);
            safeThis->aiButton_.setButtonText("Generate");
            safeThis->aiPromptEditor_.setEnabled(true);

            if (!response.success || response.text.isEmpty()) {
                DBG("AI pattern generation failed: " + response.error);
                safeThis->aiStatusLabel_.setText("Error: " + response.error,
                                                 juce::dontSendNotification);
                return;
            }

            // Parse JSON response — strip markdown fences if present
            auto text = response.text.trim();
            if (text.startsWith("```")) {
                auto firstNewline = text.indexOf("\n");
                auto lastFence = text.lastIndexOf("```");
                if (firstNewline >= 0 && lastFence > firstNewline)
                    text = text.substring(firstNewline + 1, lastFence).trim();
            }

            auto json = juce::JSON::parse(text);
            // Try "steps" key first, then treat root as array
            auto* stepsArray = json.getProperty("steps", {}).getArray();
            if (!stepsArray)
                stepsArray = json.getArray();
            if (!stepsArray) {
                DBG("AI response missing 'steps' array: " + text.substring(0, 200));
                safeThis->aiStatusLabel_.setText("Error: failed to parse response",
                                                 juce::dontSendNotification);
                return;
            }

            std::vector<SeqPlugin::Step> steps;
            for (int i = 0; i < std::min((int)stepsArray->size(), numSteps); ++i) {
                auto s = (*stepsArray)[i];
                SeqPlugin::Step step;
                step.noteNumber = juce::jlimit(0, 127, (int)s.getProperty("note", 60));
                step.octaveShift = juce::jlimit(-2, 2, (int)s.getProperty("octave", 0));
                step.gate = (bool)s.getProperty("gate", true);
                step.accent = (bool)s.getProperty("accent", false);
                step.glide = (bool)s.getProperty("glide", false);
                step.tie = (bool)s.getProperty("tie", false);
                steps.push_back(step);
            }

            if (safeThis->plugin_ && !steps.empty()) {
                safeThis->plugin_->setPattern(steps);

                // Show the description in the status label
                auto description = json.getProperty("description", {}).toString().trim();
                safeThis->aiStatusLabel_.setText(description.isNotEmpty() ? description
                                                                          : "Pattern generated",
                                                 juce::dontSendNotification);

                safeThis->repaint();
            }
        });
    }).detach();
}

// =============================================================================
// Context menu
// =============================================================================

void StepSequencerUI::showStepContextMenu(int stepIndex) {
    if (!plugin_)
        return;

    int count = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());
    if (stepIndex < 0 || stepIndex >= count)
        return;

    auto step = plugin_->getStep(stepIndex);

    juce::PopupMenu menu;
    menu.addItem(1, step.gate ? "Mute Step" : "Unmute Step");
    menu.addItem(2, "Copy to All Steps");
    menu.addItem(3, "Clear Step");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, stepIndex, count](int result) {
        if (!plugin_)
            return;
        switch (result) {
            case 1: {
                auto s = plugin_->getStep(stepIndex);
                plugin_->setStepGate(stepIndex, !s.gate);
                break;
            }
            case 2: {
                auto src = plugin_->getStep(stepIndex);
                for (int i = 0; i < count; ++i) {
                    if (i == stepIndex)
                        continue;
                    plugin_->setStepNote(i, src.noteNumber);
                    plugin_->setStepOctaveShift(i, src.octaveShift);
                    plugin_->setStepGate(i, src.gate);
                    plugin_->setStepAccent(i, src.accent);
                    plugin_->setStepGlide(i, src.glide);
                    plugin_->setStepTie(i, src.tie);
                }
                break;
            }
            case 3:
                plugin_->clearStep(stepIndex);
                break;
            default:
                return;
        }
        repaint();
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

std::vector<LinkableTextSlider*> StepSequencerUI::getLinkableSliders() {
    magda::ChainNodePath dummy;
    // Param indices match AutomatableParameter registration order:
    // 0=rate, 1=direction, 2=swing, 3=glidetime, 4=accentvel, 5=normalvel, 6=ramp, 7=skew
    rateSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 0, dummy);
    swingSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 2, dummy);
    glideSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 3, dummy);
    depthSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 6, dummy);
    skewSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 7, dummy);
    return {&rateSlider_, &swingSlider_, &glideSlider_, &depthSlider_, &skewSlider_};
}

}  // namespace magda::daw::ui
