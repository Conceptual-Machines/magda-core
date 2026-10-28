// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/StepSequencerUI.hpp"

namespace magda::daw::ui {

using SeqPlugin = daw::audio::StepSequencerPlugin;
namespace sf = sequencer_faceplate;

std::atomic<int> StepSequencerUI::nextPatternGesture_{magda::kNoStepPatternGesture + 1};

namespace {

const bool kIsBlack[] = {false, true,  false, true,  false, false,
                         true,  false, true,  false, true,  false};

juce::Rectangle<int> columnsOf(juce::Rectangle<int> row) {
    return row.withTrimmedLeft(sf::kGutterWidth);
}

}  // namespace

// =============================================================================
// Construction
// =============================================================================

StepSequencerUI::StepSequencerUI() {
    panel_.rate.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kRate, static_cast<float>(juce::roundToInt(value)));
    };
    panel_.steps.onValueChanged = [this](double value) {
        const int steps = juce::jlimit(1, SeqPlugin::MAX_STEPS, juce::roundToInt(value));
        // How many steps play is part of the pattern, so it is a pattern edit. A drag coalesces
        // into one undo entry; a typed or clicked value stays discrete (#2335).
        editPattern(
            "Set Step Count", [steps](step_pattern::MonoPattern& p) { p.length = steps; },
            panel_.steps.isBeingDragged() ? magda::StepPatternGesture::Continuous
                                          : magda::StepPatternGesture::Discrete);
        repaint();
    };
    panel_.steps.getSlider().onDragEnd = [this] { endPatternGesture(); };
    panel_.swing.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kSwing, static_cast<float>(value));
    };
    panel_.gate.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kGateLength, static_cast<float>(value));
    };
    panel_.direction.onChange = [this] {
        sendChange(SeqPlugin::kDirection, static_cast<float>(panel_.direction.getSelectedId() - 1));
    };
    panel_.quantize.onValueChanged = [this](double) { settingsEdited(); };
    panel_.subdivision.onValueChanged = [this](double) { settingsEdited(); };

    panel_.bendAmount.onValueChanged = [this](double value) {
        auto bend = panel_.timeBend.getBend();
        bend.amount = static_cast<float>(value);
        panel_.timeBend.setBend(bend);
        sendChange(SeqPlugin::kBendAmount, bend.amount);
    };
    panel_.bendCycles.onValueChanged = [this](double value) {
        auto bend = panel_.timeBend.getBend();
        bend.cycles = juce::roundToInt(value);
        panel_.timeBend.setBend(bend);
        sendChange(SeqPlugin::kBendCycles, static_cast<float>(bend.cycles));
    };
    panel_.timeBend.onCommit = [this](const auto& before, const auto& after) {
        timeBendEdited(before, after);
    };

    for (auto [slider, slot] : {std::pair{&velocity_, SeqPlugin::kNormalVelocity},
                                std::pair{&accentVelocity_, SeqPlugin::kAccentVelocity}}) {
        slider->setRange(1.0, 127.0, 1.0);
        slider->setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
        slider->setValueParser([](const juce::String& t) { return t.trim().getDoubleValue(); });
        slider->onValueChanged = [this, slot](double value) {
            sendChange(slot, static_cast<float>(juce::roundToInt(value)));
        };
        sf::styleTileValue(*slider);
    }
    panel_.addExtraTile("Velocity", velocity_);
    panel_.addExtraTile("Accent", accentVelocity_);
    addAndMakeVisible(panel_);

    // MIDI thru, step record and pattern randomize live in the device-slot header.
}

StepSequencerUI::~StepSequencerUI() {
    stopTimer();
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
    // By paramIndex, not array position: the model's list is in the saved
    // document's order and need not be complete, so a positional read draws one
    // slot's value on another's control (#2335).
    const auto display = [&params](int index, float fallback) {
        for (const auto& parameter : params)
            if (parameter.paramIndex == index)
                return parameter.currentValue;
        return fallback;
    };

    panel_.rate.setValue(static_cast<double>(display(SeqPlugin::kRate, 7.0f)),
                         juce::dontSendNotification);
    panel_.direction.setSelectedId(juce::roundToInt(display(SeqPlugin::kDirection, 0.0f)) + 1,
                                   juce::dontSendNotification);
    panel_.swing.setValue(static_cast<double>(display(SeqPlugin::kSwing, 0.0f)),
                          juce::dontSendNotification);
    panel_.gate.setValue(static_cast<double>(display(SeqPlugin::kGateLength, 0.8f)),
                         juce::dontSendNotification);

    velocity_.setValue(static_cast<double>(display(SeqPlugin::kNormalVelocity, 90.0f)),
                       juce::dontSendNotification);
    accentVelocity_.setValue(static_cast<double>(display(SeqPlugin::kAccentVelocity, 120.0f)),
                             juce::dontSendNotification);

    auto bend = panel_.timeBend.getBend();
    bend.amount = display(SeqPlugin::kBendAmount, 1.0f);
    bend.cycles = juce::roundToInt(display(SeqPlugin::kBendCycles, 1.0f));
    panel_.timeBend.setBend(bend);
    panel_.bendAmount.setValue(bend.amount, juce::dontSendNotification);
    panel_.bendCycles.setValue(bend.cycles, juce::dontSendNotification);

    syncSettingsFromDevice();
    repaint();
}

void StepSequencerUI::setPattern(const step_pattern::MonoPattern& pattern) {
    // The controls are written whether or not the pattern moved. Returning
    // early on an unchanged pattern left them at their own defaults for a
    // pattern that happens to equal the default one, which reads as a 16-step
    // pattern under a Steps of 1 (#2602). Only the repaint is worth skipping.
    const bool changed = !(pattern == pattern_);

    pattern_ = pattern;
    const int steps = pattern_.playingLength();
    panel_.steps.setValue(static_cast<double>(steps), juce::dontSendNotification);

    if (changed)
        repaint();
}

void StepSequencerUI::syncSettingsFromDevice() {
    if (device_ == nullptr)
        return;

    auto bend = device_->timeBend.current();
    bend.amount = panel_.timeBend.getBend().amount;
    bend.cycles = panel_.timeBend.getBend().cycles;
    panel_.timeBend.setBend(bend);
    panel_.quantize.setValue(static_cast<double>(device_->quantize.load(std::memory_order_relaxed)),
                             juce::dontSendNotification);
    panel_.subdivision.setValue(
        static_cast<double>(device_->quantizeSub.load(std::memory_order_relaxed)),
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
    settings.set(daw::audio::DeviceTimeBend::settingId,
                 juce::String(sdk::sequencer::timeBendToText(panel_.timeBend.getBend())));
    settings.set(IDs::quantize, static_cast<float>(panel_.quantize.getValue()));
    settings.set(IDs::quantizeSub, juce::roundToInt(panel_.subdivision.getValue()));
    onSettingsEdited(settings);
}

void StepSequencerUI::timeBendEdited(const sdk::sequencer::TimeBend& before,
                                     const sdk::sequencer::TimeBend& after) {
    if (after.amount != before.amount)
        sendChange(SeqPlugin::kBendAmount, after.amount);
    if (after.cycles != before.cycles)
        sendChange(SeqPlugin::kBendCycles, static_cast<float>(after.cycles));
    auto rest = after;
    rest.amount = before.amount;
    rest.cycles = before.cycles;
    if (!(rest == before))
        settingsEdited();
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
            if (recorded.stepIndex < 0 || recorded.stepIndex >= sdk::sequencer::kMaxSteps)
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
    panel_.timeBend.setPlayheadPhase(device_->bendPhase_.load(std::memory_order_relaxed));
    panel_.timeBend.setCycle(device_->bendCycleBeats_.load(std::memory_order_relaxed),
                             device_->bendBeatsPerBar_.load(std::memory_order_relaxed));
    panel_.timeBend.setDefaultDivision(device_->bendStepBeats_.load(std::memory_order_relaxed));

    const int numSteps = pattern_.playingLength();
    int step = device_->currentPlayStep_.load(std::memory_order_relaxed);
    bool needsRepaint = false;
    if (step != currentPlayStep_) {
        currentPlayStep_ = step;
        needsRepaint = true;
    }
    // The step recorder's position is the selected step, and the header banner shows it.
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

int StepSequencerUI::keyboardOctaves() const {
    return juce::jlimit(2, 4, columnsOf(keyboardArea_).getWidth() / 150);
}

void StepSequencerUI::setTimeBendShown(bool shown) {
    if (shown == panel_.showsTimeBend())
        return;
    panel_.setShowsTimeBend(shown);
    resized();
    repaint();
}

int StepSequencerUI::preferredWidth() const {
    return 2 * sf::kInsetX + sf::kStepsWidth + sf::kStepsGap + panel_.preferredWidth();
}

void StepSequencerUI::resized() {
    auto bounds = getLocalBounds().reduced(sf::kInsetX, sf::kInsetY);

    panel_.setBounds(bounds.removeFromRight(panel_.preferredWidth()));
    bounds.removeFromRight(sf::kStepsGap);
    dividerX_ = bounds.getRight() + sf::kStepsGap / 2;

    rulerArea_ = bounds.removeFromTop(sf::kRulerHeight);
    bounds.removeFromTop(2);
    noteArea_ = bounds.removeFromTop(NOTE_ROW_HEIGHT);
    bounds.removeFromTop(sf::kRowGap + 3);

    const auto toggleRow = [&bounds] {
        auto row = bounds.removeFromBottom(sf::kToggleRowHeight);
        bounds.removeFromBottom(sf::kRowGap);
        return row;
    };
    accentArea_ = toggleRow();
    glideArea_ = toggleRow();
    tieArea_ = toggleRow();
    gateArea_ = toggleRow();
    bounds.removeFromBottom(3);

    const int keyboardH =
        juce::jlimit(MIN_KEYBOARD_HEIGHT, MAX_KEYBOARD_HEIGHT, bounds.getHeight());
    keyboardArea_ = bounds.removeFromTop(keyboardH);

    auto gutter = keyboardArea_.withWidth(sf::kGutterWidth - 6);
    octaveLabelArea_ = gutter.removeFromTop(18);
    auto buttons = gutter.removeFromTop(OCTAVE_BUTTON_HEIGHT).withTrimmedLeft(4);
    octaveDownArea_ = buttons.removeFromLeft(buttons.getWidth() / 2);
    octaveUpArea_ = buttons;

    // A narrower keyboard shows fewer octaves; keep the one it shows inside MIDI.
    keyboardBaseNote_ = juce::jlimit(MIN_BASE_NOTE, 128 - keyboardNoteCount(), keyboardBaseNote_);
}

// =============================================================================
// Paint
// =============================================================================

void StepSequencerUI::paint(juce::Graphics& g) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.drawVerticalLine(dividerX_, 0.0f, static_cast<float>(getHeight()));

    const int count = pattern_.playingLength();
    sf::drawRuler(g, columnsOf(rulerArea_), count, currentPlayStep_);

    sf::drawRowLabel(g, noteArea_.withWidth(sf::kGutterWidth), "Note");
    drawNoteRow(g);

    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    g.drawText("Octave", octaveLabelArea_.withTrimmedRight(2), juce::Justification::centredRight);
    drawOctaveButtons(g);
    drawKeyboard(g);

    const auto& steps = pattern_;
    const auto row = [&](juce::Rectangle<int> area, const char* label, magda::ColourRole role,
                         auto isOn) {
        sf::drawRowLabel(g, area.withWidth(sf::kGutterWidth), label);
        sf::drawToggleRow(g, columnsOf(area), count, role, isOn);
    };
    row(gateArea_, "Gate", ActiveTheme::DEVICE_GREEN, [&](int i) { return steps.step(i).gate; });
    row(tieArea_, "Tie", ActiveTheme::DEVICE_BLUE, [&](int i) { return steps.step(i).tie; });
    row(glideArea_, "Glide", ActiveTheme::DEVICE_VIOLET,
        [&](int i) { return steps.step(i).glide; });
    row(accentArea_, "Accent", ActiveTheme::DEVICE_AMBER,
        [&](int i) { return steps.step(i).accent; });
}

void StepSequencerUI::drawNoteRow(juce::Graphics& g) {
    const auto columns = columnsOf(noteArea_);
    const int count = pattern_.playingLength();
    const auto green = ActiveTheme::getColour(ActiveTheme::DEVICE_GREEN);
    const float colW = static_cast<float>(columns.getWidth()) / static_cast<float>(count);
    g.setFont(FontManager::getInstance().getMonoFont(colW < 30.0f ? 9.5f : 11.5f));

    for (int i = 0; i < count; ++i) {
        const auto& step = pattern_.step(i);
        const auto cell = sf::cellBounds(columns, i, count);

        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
        g.fillRoundedRectangle(cell, 4.0f);
        if (i == currentPlayStep_) {
            g.setColour(green.withAlpha(0.14f));
            g.fillRoundedRectangle(cell, 4.0f);
        }

        auto border = ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER);
        float thickness = 1.0f;
        if (i == dragTargetStep_ && dragSourceStep_ >= 0 && i != dragSourceStep_) {
            border = ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE);
            thickness = 1.5f;
        } else if (i == selectedStep_) {
            border = green;
            thickness = 1.5f;
        }
        g.setColour(border);
        g.drawRoundedRectangle(cell.reduced(thickness * 0.5f), 4.0f, thickness);

        // A rest reads as a dot; a tie carries the note before it, so it is dimmed.
        const bool sounds = step.gate && !step.tie;
        g.setColour(ActiveTheme::getColour(sounds ? ActiveTheme::DEVICE_VALUE_TEXT
                                                  : ActiveTheme::DEVICE_DIM2));
        g.drawText(step.gate ? sf::noteName(step.noteNumber + step.octaveShift * 12)
                             : juce::String::fromUTF8("\xc2\xb7"),
                   cell.toNearestInt(), juce::Justification::centred, false);
    }
}

void StepSequencerUI::drawKeyboard(juce::Graphics& g) {
    const auto area = columnsOf(keyboardArea_).toFloat();
    if (area.isEmpty())
        return;

    const int numNotes = keyboardNoteCount();
    const float whiteW = area.getWidth() / static_cast<float>(keyboardOctaves() * 7);
    const float blackW = whiteW * 0.62f;
    const float blackH = area.getHeight() * 0.6f;
    const auto& selected = pattern_.step(selectedStep_);
    const int selectedNote = selected.noteNumber + selected.octaveShift * 12;
    const auto green = ActiveTheme::getColour(ActiveTheme::DEVICE_GREEN);

    juce::Path outline;
    outline.addRoundedRectangle(area, 5.0f);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(outline);

    g.setFont(FontManager::getInstance().getMonoFont(9.5f));
    int whiteIdx = 0;
    for (int n = 0; n < numNotes; ++n) {
        if (kIsBlack[n % 12])
            continue;
        const int note = keyboardBaseNote_ + n;
        const juce::Rectangle<float> key{area.getX() + whiteIdx * whiteW, area.getY(), whiteW,
                                         area.getHeight()};
        const bool lit = note == selectedNote;
        g.setColour(lit ? green : sf::kWhiteKey);
        g.fillRect(key);
        g.setColour(sf::kBlackKey.withAlpha(0.35f));
        g.drawVerticalLine(juce::roundToInt(key.getRight()), key.getY(), key.getBottom());
        if (n % 12 == 0 || lit) {
            g.setColour(lit ? sf::kWhiteKey : sf::kBlackKey.withAlpha(0.6f));
            g.drawText(sf::noteName(note), key.reduced(3.0f, 3.0f).toNearestInt(),
                       juce::Justification::bottomLeft, false);
        }
        ++whiteIdx;
    }

    whiteIdx = 0;
    for (int n = 0; n < numNotes; ++n) {
        if (!kIsBlack[n % 12]) {
            ++whiteIdx;
            continue;
        }
        const juce::Rectangle<float> key{area.getX() + whiteIdx * whiteW - blackW / 2.0f,
                                         area.getY(), blackW, blackH};
        g.setColour(keyboardBaseNote_ + n == selectedNote ? green : sf::kBlackKey);
        g.fillRoundedRectangle(key.withTrimmedTop(-3.0f), 3.0f);
    }

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.strokePath(outline, juce::PathStrokeType(1.0f));
}

void StepSequencerUI::drawOctaveButtons(juce::Graphics& g) {
    const auto box = octaveDownArea_.getUnion(octaveUpArea_).toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);

    const auto glyph = [&](juce::Rectangle<int> area, bool enabled, bool plus) {
        g.setColour(ActiveTheme::getColour(enabled ? ActiveTheme::DEVICE_TEXT
                                                   : ActiveTheme::DEVICE_ICON_INACTIVE));
        const auto c = area.getCentre().toFloat();
        g.drawLine(c.x - 4.0f, c.y, c.x + 4.0f, c.y, 1.4f);
        if (plus)
            g.drawLine(c.x, c.y - 4.0f, c.x, c.y + 4.0f, 1.4f);
    };
    glyph(octaveDownArea_, keyboardBaseNote_ > MIN_BASE_NOTE, false);
    glyph(octaveUpArea_, keyboardBaseNote_ + keyboardNoteCount() + 12 <= 128, true);
}

// =============================================================================
// Mouse interaction
// =============================================================================

void StepSequencerUI::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    const int count = pattern_.playingLength();

    // Step boxes: select, menu on right-click, shift+drag to copy.
    if (columnsOf(noteArea_).contains(pos)) {
        const int step = sf::stepAt(pos.x, columnsOf(noteArea_), count);
        if (step < 0)
            return;
        selectedStep_ = step;
        if (e.mods.isPopupMenu()) {
            showStepContextMenu(step);
        } else if (e.mods.isShiftDown()) {
            dragSourceStep_ = step;
            dragTargetStep_ = step;
        }
        repaint();
        return;
    }

    if (octaveDownArea_.contains(pos) || octaveUpArea_.contains(pos)) {
        const int shift = octaveUpArea_.contains(pos) ? 12 : -12;
        const int next = keyboardBaseNote_ + shift;
        if (next >= MIN_BASE_NOTE && next + keyboardNoteCount() <= 128) {
            keyboardBaseNote_ = next;
            repaint();
        }
        return;
    }

    if (columnsOf(keyboardArea_).contains(pos)) {
        const int note = getKeyboardNoteAtPosition(pos);
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

    toggleStepFlag(e);
}

void StepSequencerUI::toggleStepFlag(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    const int count = pattern_.playingLength();
    const auto stepIn = [&](juce::Rectangle<int> row) {
        return row.contains(pos) ? sf::stepAt(pos.x, columnsOf(row), count) : -1;
    };

    if (const int step = stepIn(gateArea_); step >= 0) {
        editPattern("Toggle Step Gate", [step](step_pattern::MonoPattern& p) {
            auto& target = p.steps[static_cast<size_t>(step)];
            target.gate = !target.gate;
        });
    } else if (const int tieStep = stepIn(tieArea_); tieStep >= 0) {
        // Tie and glide are mutually exclusive.
        editPattern("Toggle Step Tie", [tieStep](step_pattern::MonoPattern& p) {
            auto& target = p.steps[static_cast<size_t>(tieStep)];
            target.tie = !target.tie;
            if (target.tie)
                target.glide = false;
        });
    } else if (const int glideStep = stepIn(glideArea_); glideStep >= 0) {
        editPattern("Toggle Step Glide", [glideStep](step_pattern::MonoPattern& p) {
            auto& target = p.steps[static_cast<size_t>(glideStep)];
            target.glide = !target.glide;
            if (target.glide)
                target.tie = false;
        });
    } else if (const int accentStep = stepIn(accentArea_); accentStep >= 0) {
        editPattern("Toggle Step Accent", [accentStep](step_pattern::MonoPattern& p) {
            auto& target = p.steps[static_cast<size_t>(accentStep)];
            target.accent = !target.accent;
        });
    } else {
        return;
    }
    repaint();
}

void StepSequencerUI::mouseDrag(const juce::MouseEvent& e) {
    if (dragSourceStep_ < 0)
        return;

    const int step = sf::stepAt(e.x, columnsOf(noteArea_), pattern_.playingLength());
    if (step >= 0 && step != dragTargetStep_) {
        dragTargetStep_ = step;
        repaint();
    }
}

void StepSequencerUI::mouseUp(const juce::MouseEvent&) {
    const int count = pattern_.playingLength();
    if (dragSourceStep_ >= 0 && dragTargetStep_ >= 0 && dragTargetStep_ < count &&
        dragTargetStep_ != dragSourceStep_) {
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

int StepSequencerUI::getKeyboardNoteAtPosition(juce::Point<int> pos) const {
    const auto area = columnsOf(keyboardArea_).toFloat();
    const float whiteW = area.getWidth() / static_cast<float>(keyboardOctaves() * 7);
    const float blackW = whiteW * 0.62f;
    const float blackH = area.getHeight() * 0.6f;
    const auto point = pos.toFloat();

    // Black keys sit over the white ones, so they are hit first.
    int whiteIdx = 0;
    for (int n = 0; n < keyboardNoteCount(); ++n) {
        if (!kIsBlack[n % 12]) {
            ++whiteIdx;
            continue;
        }
        const juce::Rectangle<float> key{area.getX() + whiteIdx * whiteW - blackW / 2.0f,
                                         area.getY(), blackW, blackH};
        if (key.contains(point))
            return keyboardBaseNote_ + n;
    }

    whiteIdx = 0;
    for (int n = 0; n < keyboardNoteCount(); ++n) {
        if (kIsBlack[n % 12])
            continue;
        const juce::Rectangle<float> key{area.getX() + whiteIdx * whiteW, area.getY(), whiteW,
                                         area.getHeight()};
        if (key.contains(point))
            return keyboardBaseNote_ + n;
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

std::vector<LinkableTextSlider*> StepSequencerUI::getLinkableSliders() {
    magda::ChainNodePath dummy;
    const std::pair<LinkableTextSlider*, int> linked[] = {
        {&panel_.rate, SeqPlugin::kRate},
        {&panel_.swing, SeqPlugin::kSwing},
        {&panel_.gate, SeqPlugin::kGateLength},
        {&accentVelocity_, SeqPlugin::kAccentVelocity},
        {&velocity_, SeqPlugin::kNormalVelocity},
        {&panel_.bendAmount, SeqPlugin::kBendAmount},
        {&panel_.bendCycles, SeqPlugin::kBendCycles}};
    std::vector<LinkableTextSlider*> sliders;
    for (const auto& [slider, slot] : linked) {
        slider->setLinkContext(magda::INVALID_DEVICE_ID, slot, dummy);
        sliders.push_back(slider);
    }
    return sliders;
}

}  // namespace magda::daw::ui
