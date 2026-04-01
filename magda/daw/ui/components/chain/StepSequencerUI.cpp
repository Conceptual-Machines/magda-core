#include "StepSequencerUI.hpp"

#include "audio/StepClock.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

using SeqPlugin = daw::audio::StepSequencerPlugin;

static const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

juce::String StepSequencerUI::noteNameShort(int noteNumber) {
    if (noteNumber < 0 || noteNumber > 127)
        return "-";
    int octave = (noteNumber / 12) - 1;
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

    setupLabel(glideLabel_, "GLIDE");
    setupSlider(glideSlider_, 0.0, 1.0, 0.01);
    glideSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    glideSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    glideSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->glideTime = static_cast<float>(value);
    };
}

StepSequencerUI::~StepSequencerUI() {
    stopTimer();
    if (watchedState_.isValid())
        watchedState_.removeListener(this);
    dirCombo_.setLookAndFeel(nullptr);
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
    glideSlider_.setValue(static_cast<double>(plugin_->glideTime.get()),
                          juce::dontSendNotification);
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

    // Step boxes
    stepBoxArea_ = bounds.removeFromTop(STEP_BOX_SIZE);
    bounds.removeFromTop(ROW_GAP);

    // Accent row
    accentArea_ = bounds.removeFromTop(TOGGLE_ROW_HEIGHT);
    bounds.removeFromTop(ROW_GAP);

    // Glide row
    glideArea_ = bounds.removeFromTop(TOGGLE_ROW_HEIGHT);
    bounds.removeFromTop(ROW_GAP + 2);

    // Keyboard
    keyboardArea_ = bounds.removeFromTop(KEYBOARD_HEIGHT);
    bounds.removeFromTop(ROW_GAP);

    // Octave row
    octaveArea_ = bounds.removeFromTop(OCTAVE_ROW_HEIGHT);
}

// =============================================================================
// Paint
// =============================================================================

void StepSequencerUI::paint(juce::Graphics& g) {
    drawStepBoxes(g, stepBoxArea_);
    drawAccentRow(g, accentArea_);
    drawGlideRow(g, glideArea_);
    drawKeyboard(g, keyboardArea_);
    drawOctaveRow(g, octaveArea_);
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

void StepSequencerUI::drawGlideRow(juce::Graphics& g, juce::Rectangle<int> area) {
    if (!plugin_ || area.isEmpty())
        return;

    int count = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());
    auto font = FontManager::getInstance().getUIFont(7.0f);
    g.setFont(font);

    // Row label
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText("GLD", area.removeFromLeft(24), juce::Justification::centredLeft);

    float boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        auto step = plugin_->getStep(i);
        float x = static_cast<float>(area.getX()) + i * boxW;
        auto rect =
            juce::Rectangle<float>(x + 1.0f, static_cast<float>(area.getY()) + 1.0f, boxW - 2.0f,
                                   static_cast<float>(area.getHeight()) - 2.0f);

        if (step.glide) {
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

    // Draw a 2-octave mini keyboard (C3-B4)
    // White keys: C D E F G A B × 2 = 14 white keys
    // Black keys overlay on top

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

    // Draw white keys
    int whiteIdx = 0;
    for (int note = 0; note < KEYBOARD_NUM_NOTES; ++note) {
        if (isBlack[note % 12])
            continue;

        float x = area.getX() + whiteIdx * whiteKeyW;
        auto keyRect =
            juce::Rectangle<float>(x, static_cast<float>(area.getY()), whiteKeyW - 1.0f, whiteKeyH);

        int midiNote = KEYBOARD_BASE_NOTE + note;
        bool isSelected = plugin_ && plugin_->getStep(selectedStep_).noteNumber == midiNote;

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

        // Black key sits between the previous and current white key
        float x = area.getX() + (whiteIdx - 1) * whiteKeyW + whiteKeyW - blackKeyW / 2.0f;
        auto keyRect =
            juce::Rectangle<float>(x, static_cast<float>(area.getY()), blackKeyW, blackKeyH);

        int midiNote = KEYBOARD_BASE_NOTE + note;
        bool isSelected = plugin_ && plugin_->getStep(selectedStep_).noteNumber == midiNote;

        g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.6f)
                               : juce::Colours::black.withAlpha(0.85f));
        g.fillRoundedRectangle(keyRect, 1.0f);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.2f));
        g.drawRoundedRectangle(keyRect, 1.0f, 0.5f);
    }
}

void StepSequencerUI::drawOctaveRow(juce::Graphics& g, juce::Rectangle<int> area) {
    if (!plugin_ || area.isEmpty())
        return;

    auto font = FontManager::getInstance().getUIFont(8.0f);
    g.setFont(font);

    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText("OCT", area.removeFromLeft(24), juce::Justification::centredLeft);

    int currentOctave = plugin_->getStep(selectedStep_).octaveShift;

    // Draw octave buttons: -2, -1, 0, +1, +2
    int btnW = std::min(32, area.getWidth() / 5);
    for (int oct = -2; oct <= 2; ++oct) {
        auto btn = area.removeFromLeft(btnW).reduced(1);
        bool isActive = (oct == currentOctave);

        g.setColour(isActive ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.5f)
                             : DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
        g.fillRoundedRectangle(btn.toFloat(), 2.0f);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
        g.drawRoundedRectangle(btn.toFloat(), 2.0f, 0.5f);

        g.setColour(isActive ? DarkTheme::getTextColour() : DarkTheme::getSecondaryTextColour());
        juce::String label = (oct > 0) ? ("+" + juce::String(oct)) : juce::String(oct);
        g.drawText(label, btn, juce::Justification::centred);
    }
}

// =============================================================================
// Mouse interaction
// =============================================================================

void StepSequencerUI::mouseDown(const juce::MouseEvent& e) {
    if (!plugin_)
        return;
    auto pos = e.getPosition();
    int count = juce::jlimit(1, SeqPlugin::MAX_STEPS, plugin_->numSteps.get());

    // Step boxes — select step (or toggle gate on right-click)
    if (stepBoxArea_.contains(pos)) {
        int step = getStepAtX(pos.x, stepBoxArea_.getX(), stepBoxArea_.getWidth(), count);
        if (step >= 0 && step < count) {
            if (e.mods.isRightButtonDown()) {
                // Toggle gate
                auto s = plugin_->getStep(step);
                plugin_->setStepGate(step, !s.gate);
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

    // Glide row — toggle glide
    if (glideArea_.contains(pos)) {
        auto contentArea = glideArea_.withTrimmedLeft(24);
        int step = getStepAtX(pos.x, contentArea.getX(), contentArea.getWidth(), count);
        if (step >= 0 && step < count) {
            auto s = plugin_->getStep(step);
            plugin_->setStepGlide(step, !s.glide);
            repaint();
        }
        return;
    }

    // Keyboard — assign note to selected step
    if (keyboardArea_.contains(pos)) {
        int note = getKeyboardNoteAtPosition(pos, keyboardArea_);
        if (note >= 0) {
            plugin_->setStepNote(selectedStep_, note);
            plugin_->setStepOctaveShift(selectedStep_, 0);  // Reset octave shift when setting note
            repaint();
        }
        return;
    }

    // Octave row — set octave shift for selected step
    if (octaveArea_.contains(pos)) {
        int shift = getOctaveShiftAtPosition(pos, octaveArea_);
        if (shift >= -2 && shift <= 2) {
            plugin_->setStepOctaveShift(selectedStep_, shift);
            repaint();
        }
        return;
    }
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
            return KEYBOARD_BASE_NOTE + note;
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
            return KEYBOARD_BASE_NOTE + note;
        ++whiteIdx;
    }

    return -1;
}

int StepSequencerUI::getOctaveShiftAtPosition(juce::Point<int> pos,
                                              juce::Rectangle<int> area) const {
    auto contentArea = area.withTrimmedLeft(24);
    int btnW = std::min(32, contentArea.getWidth() / 5);

    for (int oct = -2; oct <= 2; ++oct) {
        auto btn = juce::Rectangle<int>(contentArea.getX() + (oct + 2) * btnW, contentArea.getY(),
                                        btnW, contentArea.getHeight());
        if (btn.contains(pos))
            return oct;
    }
    return -99;  // No hit
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
    // 0=rate, 1=direction, 2=swing, 3=glidetime, 4=accentvel, 5=normalvel
    rateSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 0, dummy);
    swingSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 2, dummy);
    glideSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 3, dummy);
    return {&rateSlider_, &swingSlider_, &glideSlider_};
}

}  // namespace magda::daw::ui
