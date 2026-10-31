// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/StepSequencerUI.hpp"

#include "core/GestureRouter.hpp"
#include "ui/state/ExpandedFaceplate.hpp"

namespace magda::daw::ui {

using SeqPlugin = daw::audio::StepSequencerPlugin;
namespace sf = sequencer_faceplate;

std::atomic<int> StepSequencerUI::nextPatternGesture_{magda::kNoStepPatternGesture + 1};

namespace {

juce::Rectangle<int> columnsOf(juce::Rectangle<int> row) {
    return row.withTrimmedLeft(sf::kGutterWidth);
}

}  // namespace

// =============================================================================
// Construction
// =============================================================================

StepSequencerUI::StepSequencerUI() {
    strip_.rate.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kRate, static_cast<float>(juce::roundToInt(value)));
    };
    strip_.steps.onValueChanged = [this](double value) {
        const int steps = juce::jlimit(1, SeqPlugin::MAX_STEPS, juce::roundToInt(value));
        // How many steps play is part of the pattern, so it is a pattern edit. A drag coalesces
        // into one undo entry; a typed or clicked value stays discrete (#2335).
        editPattern(
            "Set Step Count", [steps](step_pattern::MonoPattern& p) { p.length = steps; },
            strip_.steps.isBeingDragged() ? magda::StepPatternGesture::Continuous
                                          : magda::StepPatternGesture::Discrete);
        redraw();
    };
    strip_.steps.getSlider().onDragEnd = [this] { endPatternGesture(); };
    strip_.gate.onValueChanged = [this](double value) {
        sendChange(SeqPlugin::kGateLength, static_cast<float>(value));
    };
    strip_.direction.onChange = [this](int index) {
        sendChange(SeqPlugin::kDirection, static_cast<float>(index));
    };

    timeBend_.setShowsGrooves(true);
    bendExpandButton_->onClick = [this] {
        if (onExpandClicked)
            onExpandClicked(timeBend_);
    };
    timeBend_.setHeaderAccessory(bendExpandButton_.get());
    timeBend_.onCommit = [this](const auto& before, const auto& after) {
        timeBendEdited(before, after);
    };

    strip_.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(strip_);
    addAndMakeVisible(timeBend_);
    addChildComponent(keyboard_);
    expandButton_->onClick = [this] {
        if (onExpandClicked)
            onExpandClicked(expandablePart());
    };
    addAndMakeVisible(*expandButton_);

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

    strip_.rate.setValue(static_cast<double>(display(SeqPlugin::kRate, 7.0f)),
                         juce::dontSendNotification);
    strip_.direction.setSelectedIndex(juce::roundToInt(display(SeqPlugin::kDirection, 0.0f)));
    strip_.gate.setValue(static_cast<double>(display(SeqPlugin::kGateLength, 0.8f)),
                         juce::dontSendNotification);

    velocity_ = juce::roundToInt(display(SeqPlugin::kNormalVelocity, 90.0f));
    accentVelocity_ = juce::roundToInt(display(SeqPlugin::kAccentVelocity, 120.0f));

    auto bend = timeBend_.getBend();
    bend.amount = display(SeqPlugin::kBendAmount, 1.0f);
    bend.cycles = juce::roundToInt(display(SeqPlugin::kBendCycles, 1.0f));
    timeBend_.setBend(bend);

    syncSettingsFromDevice();
    redraw();
}

void StepSequencerUI::setPattern(const step_pattern::MonoPattern& pattern) {
    // The controls are written whether or not the pattern moved. Returning
    // early on an unchanged pattern left them at their own defaults for a
    // pattern that happens to equal the default one, which reads as a 16-step
    // pattern under a Steps of 1 (#2602). Only the repaint is worth skipping.
    const bool changed = !(pattern == pattern_);

    pattern_ = pattern;
    const int steps = pattern_.playingLength();
    strip_.steps.setValue(static_cast<double>(steps), juce::dontSendNotification);

    if (changed)
        redraw();
}

void StepSequencerUI::syncSettingsFromDevice() {
    if (device_ == nullptr)
        return;

    auto bend = device_->timeBend.current();
    bend.amount = timeBend_.getBend().amount;
    bend.cycles = timeBend_.getBend().cycles;
    timeBend_.setBend(bend);
    quantize_ = static_cast<double>(device_->quantize.load(std::memory_order_relaxed));
    quantizeSub_ = device_->quantizeSub.load(std::memory_order_relaxed);
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
                 juce::String(sdk::sequencer::timeBendToText(timeBend_.getBend())));
    settings.set(IDs::quantize, static_cast<float>(quantize_));
    settings.set(IDs::quantizeSub, quantizeSub_);
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
    timeBend_.setPlayheadPhase(device_->bendPhase_.load(std::memory_order_relaxed));
    timeBend_.setCycle(device_->bendCycleBeats_.load(std::memory_order_relaxed),
                       device_->bendBeatsPerBar_.load(std::memory_order_relaxed));
    timeBend_.setDefaultDivision(device_->bendStepBeats_.load(std::memory_order_relaxed));

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
        redraw();
}

// =============================================================================
// Layout
// =============================================================================

bool StepSequencerUI::keyboardLent() const {
    return magda::ExpandedFaceplate::getInstance().holds(keyboard_);
}

void StepSequencerUI::redraw() {
    repaint();
    keyboard_.repaint();
}

void StepSequencerUI::expandablePartReturned() {
    if (keyboard_.getParentComponent() != this)
        addChildComponent(keyboard_);
    if (timeBend_.getParentComponent() != this) {
        addChildComponent(timeBend_);
        timeBend_.setVisible(timeBendShown_);
    }
    resized();
    redraw();
}

bool StepSequencerUI::timeBendLent() const {
    return magda::ExpandedFaceplate::getInstance().holds(timeBend_);
}

bool StepSequencerUI::timeBendInFaceplate() const {
    return timeBendShown_ && !timeBendLent();
}

juce::Component* StepSequencerUI::lentPart() const {
    const auto& expanded = magda::ExpandedFaceplate::getInstance();
    if (expanded.holds(keyboard_))
        return expanded.faceplate();
    return expanded.holds(timeBend_) ? expanded.faceplate() : nullptr;
}

void StepSequencerUI::setTimeBendShown(bool shown) {
    if (shown == timeBendShown_)
        return;
    timeBendShown_ = shown;
    if (!timeBendLent())
        timeBend_.setVisible(shown);
    resized();
    redraw();
}

int StepSequencerUI::preferredWidth() const {
    return 2 * sf::kInsetX + sf::kStepsWidth +
           (timeBendInFaceplate() ? sf::kStepsGap + sf::kTimeBendWidth : 0);
}

int StepSequencerUI::preferredHeight() {
    return 2 * sf::kInsetY + sf::SettingsStrip::kHeight + sf::kRowGap + sf::kRulerHeight + 2 +
           sf::kNoteRowHeight + sf::kRowGap + MAX_KEYBOARD_HEIGHT +
           4 * (sf::kToggleRowHeight + sf::kRowGap);
}

void StepSequencerUI::resized() {
    auto bounds = getLocalBounds().reduced(sf::kInsetX, sf::kInsetY);

    // Out in the arrangement, the host's own button closes it.
    if (bendExpandButton_->isVisible() == timeBendLent()) {
        bendExpandButton_->setVisible(!timeBendLent());
        timeBend_.resized();
    }
    if (timeBendInFaceplate()) {
        timeBend_.setBounds(bounds.removeFromRight(sf::kTimeBendWidth));
        bounds.removeFromRight(sf::kStepsGap);
    }
    dividerX_ = timeBendInFaceplate() ? bounds.getRight() + sf::kStepsGap / 2 : -1;

    strip_.setBounds(bounds.removeFromTop(sf::SettingsStrip::kHeight));
    bounds.removeFromTop(sf::kRowGap);

    rulerArea_ = bounds.removeFromTop(sf::kRulerHeight);
    expandButton_->setBounds(sf::expandButtonBounds(rulerArea_));
    expandButton_->setToggleState(keyboardLent(), juce::dontSendNotification);
    bounds.removeFromTop(2);
    noteArea_ = bounds.removeFromTop(sf::kNoteRowHeight);
    bounds.removeFromTop(sf::kRowGap);

    const auto toggleRow = [&bounds] {
        auto row = bounds.removeFromBottom(sf::kToggleRowHeight);
        bounds.removeFromBottom(sf::kRowGap);
        return row;
    };
    accentArea_ = toggleRow();
    glideArea_ = toggleRow();
    tieArea_ = toggleRow();
    gateArea_ = toggleRow();

    // Lent to the arrangement, or short of room for a playable keyboard, the note boxes stand in.
    if (keyboardLent())
        return;
    const bool fits = bounds.getHeight() >= MIN_KEYBOARD_HEIGHT;
    keyboard_.setVisible(fits);
    if (fits)
        keyboard_.setBounds(
            bounds.removeFromTop(juce::jmin(MAX_KEYBOARD_HEIGHT, bounds.getHeight())));
}

// =============================================================================
// Paint
// =============================================================================

void StepSequencerUI::paint(juce::Graphics& g) {
    if (dividerX_ >= 0) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
        g.drawVerticalLine(dividerX_, 0.0f, static_cast<float>(getHeight()));
    }

    const int count = pattern_.playingLength();
    sf::drawRuler(g, columnsOf(rulerArea_), count, currentPlayStep_);

    sf::drawRowLabel(g, noteArea_.withWidth(sf::kGutterWidth), "Note");
    drawNoteRow(g);

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
    const float colW = static_cast<float>(columns.getWidth()) / static_cast<float>(count);
    g.setFont(FontManager::getInstance().getMonoFont(colW < 30.0f ? 9.5f : 11.5f));

    for (int i = 0; i < count; ++i) {
        const auto& step = pattern_.step(i);
        // A rest reads as a dot; a tie carries the note before it, so it is dimmed.
        sf::drawNoteCell(
            g, sf::cellBounds(columns, i, count),
            {.text = step.gate ? sf::noteName(step.noteNumber + step.octaveShift * 12)
                               : juce::String::fromUTF8("\xc2\xb7"),
             .sounds = step.gate && !step.tie,
             .playing = i == currentPlayStep_,
             .selected = i == selectedStep_,
             .dropTarget = i == dragTargetStep_ && dragSourceStep_ >= 0 && i != dragSourceStep_});
    }
}

// =============================================================================
// Note editor: the keyboard, or a piano roll in the arrangement
// =============================================================================

bool StepSequencerUI::NoteEditor::showsRoll() const {
    return magda::ExpandedFaceplate::getInstance().holds(*this);
}

int StepSequencerUI::NoteEditor::octaves() const {
    return juce::jlimit(2, 6, keysArea_.getWidth() / 150);
}

void StepSequencerUI::NoteEditor::resized() {
    auto area = getLocalBounds();
    if (showsRoll()) {
        auto strip = area.removeFromLeft(STRIP_WIDTH);
        area.removeFromLeft(4);
        pianoArea_ = area.removeFromLeft(sf::kGutterWidth - STRIP_WIDTH - 8);
        area.removeFromLeft(4);
        cellArea_ = area;
        auto shiftBox = strip.removeFromTop(BUTTON_BOX_HEIGHT);
        rollUpArea_ = shiftBox.removeFromTop(BUTTON_BOX_HEIGHT / 2);
        rollDownArea_ = shiftBox;
        auto zoomBox = strip.removeFromBottom(BUTTON_BOX_HEIGHT);
        zoomInArea_ = zoomBox.removeFromTop(BUTTON_BOX_HEIGHT / 2);
        zoomOutArea_ = zoomBox;
        return;
    }

    auto gutter = area.removeFromLeft(sf::kGutterWidth).withTrimmedRight(6);
    labelArea_ = gutter.removeFromTop(18);
    auto buttons = gutter.removeFromTop(OCTAVE_BUTTON_HEIGHT).withTrimmedLeft(4);
    downArea_ = buttons.removeFromLeft(buttons.getWidth() / 2);
    upArea_ = buttons;
    keysArea_ = area.removeFromTop(juce::jmin(MAX_KEYS_HEIGHT, area.getHeight()));

    // A narrower keyboard shows fewer octaves; keep the ones it shows inside MIDI.
    baseNote_ = juce::jlimit(0, 128 - octaves() * 12, baseNote_);
}

void StepSequencerUI::NoteEditor::paint(juce::Graphics& g) {
    if (showsRoll()) {
        paintRoll(g);
        return;
    }
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    g.drawText("Octave", labelArea_.withTrimmedRight(2), juce::Justification::centredRight);
    drawOctaveButtons(g);

    const auto& selected = owner_.pattern_.step(owner_.selectedStep_);
    const int selectedNote = selected.noteNumber + selected.octaveShift * 12;
    sf::drawKeyboard(g, keysArea_.toFloat(), baseNote_, octaves(),
                     [selectedNote](int note) { return note == selectedNote; });
}

void StepSequencerUI::NoteEditor::drawOctaveButtons(juce::Graphics& g) {
    const auto box = downArea_.getUnion(upArea_).toFloat();
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
    glyph(downArea_, baseNote_ > 0, false);
    glyph(upArea_, baseNote_ + octaves() * 12 + 12 <= 128, true);
}

void StepSequencerUI::NoteEditor::mouseDown(const juce::MouseEvent& e) {
    if (showsRoll()) {
        rollMouseDown(e);
        return;
    }
    const auto pos = e.getPosition();
    if (downArea_.contains(pos) || upArea_.contains(pos)) {
        const int next = baseNote_ + (upArea_.contains(pos) ? 12 : -12);
        if (next >= 0 && next + octaves() * 12 <= 128) {
            baseNote_ = next;
            repaint();
        }
        return;
    }
    if (keysArea_.contains(pos))
        owner_.setStepNote(owner_.selectedStep_, sf::keyboardNoteAt(keysArea_.toFloat(), baseNote_,
                                                                    octaves(), pos.toFloat()));
}

void StepSequencerUI::NoteEditor::paintRoll(juce::Graphics& g) {
    if (cellArea_.isEmpty())
        return;

    sf::drawButtonPair(g, rollUpArea_.getUnion(rollDownArea_), sf::Glyph::Up,
                       lowNote_ < 128 - visibleNotes_, sf::Glyph::Down, lowNote_ > 0);
    sf::drawButtonPair(g, zoomInArea_.getUnion(zoomOutArea_), sf::Glyph::Plus,
                       visibleNotes_ > MIN_VISIBLE_NOTES, sf::Glyph::Minus,
                       visibleNotes_ < MAX_VISIBLE_NOTES);

    const auto& pattern = owner_.pattern_;
    const int count = pattern.playingLength();
    const float rowH = static_cast<float>(cellArea_.getHeight()) / visibleNotes_;
    const float colW = static_cast<float>(cellArea_.getWidth()) / static_cast<float>(count);
    const auto noteOfRow = [this](int row) { return lowNote_ + (visibleNotes_ - 1 - row); };

    g.setFont(FontManager::getInstance().getMonoFont(juce::jlimit(7.0f, 9.5f, rowH)));
    for (int row = 0; row < visibleNotes_; ++row) {
        const int note = noteOfRow(row);
        const juce::Rectangle<float> key{static_cast<float>(pianoArea_.getX()),
                                         pianoArea_.getY() + row * rowH,
                                         static_cast<float>(pianoArea_.getWidth()), rowH};
        g.setColour(sf::isBlackKey(note) ? sf::kBlackKey : sf::kWhiteKey);
        g.fillRect(key.reduced(0.0f, 0.5f));
        if (note % 12 == 0) {
            g.setColour(sf::kBlackKey.withAlpha(0.7f));
            g.drawText(sf::noteName(note), key.withTrimmedLeft(3.0f).toNearestInt(),
                       juce::Justification::centredLeft, false);
        }
    }

    sf::drawGridFloor(
        g, cellArea_, count, visibleNotes_, [&](int row) { return sf::isBlackKey(noteOfRow(row)); },
        [&pattern](int i) { return !pattern.step(i).gate; });

    const auto green = ActiveTheme::getColour(ActiveTheme::DEVICE_GREEN);
    g.setColour(green.withAlpha(0.08f));
    g.fillRect(juce::Rectangle<float>(cellArea_.getX() + owner_.selectedStep_ * colW,
                                      static_cast<float>(cellArea_.getY()), colW,
                                      static_cast<float>(cellArea_.getHeight())));

    // Each note as a bar running on through the gated ties that hold it.
    const float barH = juce::jmax(3.0f, rowH - 4.0f);
    for (int i = 0; i < count; ++i) {
        const auto& step = pattern.step(i);
        if (!step.gate || (step.tie && i > 0))
            continue;
        int end = i + 1;
        while (end < count && pattern.step(end).tie && pattern.step(end).gate)
            ++end;
        const int row = visibleNotes_ - 1 - (step.noteNumber + step.octaveShift * 12 - lowNote_);
        if (row < 0 || row >= visibleNotes_)
            continue;
        sf::drawNoteBar(g,
                        {cellArea_.getX() + i * colW + 2.0f,
                         cellArea_.getY() + row * rowH + (rowH - barH) / 2, (end - i) * colW - 4.0f,
                         barH},
                        step.accent ? 127 : 90, true);
    }

    sf::drawPlayhead(g, cellArea_, count, owner_.currentPlayStep_);
}

void StepSequencerUI::NoteEditor::rollMouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (zoomInArea_.contains(pos) || zoomOutArea_.contains(pos)) {
        zoomBy(zoomInArea_.contains(pos) ? -2 : 2);
        return;
    }
    if (rollUpArea_.contains(pos) || rollDownArea_.contains(pos)) {
        shiftWindow(rollUpArea_.contains(pos) ? 12 : -12);
        return;
    }
    if (!cellArea_.contains(pos))
        return;

    const int step = sf::stepAt(pos.x, cellArea_, owner_.pattern_.playingLength());
    const int note =
        lowNote_ + visibleNotes_ - 1 -
        (pos.y - cellArea_.getY()) * visibleNotes_ / juce::jmax(1, cellArea_.getHeight());
    if (step < 0 || note < 0 || note > 127)
        return;

    owner_.selectedStep_ = step;
    if (e.mods.isPopupMenu()) {
        owner_.showStepContextMenu(step);
        owner_.redraw();
        return;
    }

    // A step holds one note: clicking it again rests the step, anywhere else moves it there.
    const auto& held = owner_.pattern_.step(step);
    if (held.gate && held.noteNumber + held.octaveShift * 12 == note) {
        owner_.editPattern("Toggle Step Gate", [step](step_pattern::MonoPattern& p) {
            p.steps[static_cast<size_t>(step)].gate = false;
        });
    } else {
        owner_.editPattern("Set Step Note", [step, note](step_pattern::MonoPattern& p) {
            auto& target = p.steps[static_cast<size_t>(step)];
            target.noteNumber = note;
            target.octaveShift = 0;
            target.gate = true;
        });
    }
    owner_.redraw();
}

void StepSequencerUI::NoteEditor::mouseWheelMove(const juce::MouseEvent& e,
                                                 const juce::MouseWheelDetails& wheel) {
    if (!showsRoll()) {
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }
    const auto gesture = magda::GestureRouter::getInstance().resolve(
        magda::GestureContext::StepSequencer, wheel, e.mods, e.getPosition());
    const int steps = magda::quantizedGestureStep(gesture.magnitude);
    if (gesture.type == magda::GestureActionType::ZoomVertical)
        zoomBy(-2 * steps);
    else if (gesture.type == magda::GestureActionType::ScrollVertical)
        shiftWindow(2 * steps);
    else
        juce::Component::mouseWheelMove(e, wheel);
}

void StepSequencerUI::NoteEditor::shiftWindow(int semitones) {
    const int next = juce::jlimit(0, 128 - visibleNotes_, lowNote_ + semitones);
    if (next != lowNote_) {
        lowNote_ = next;
        repaint();
    }
}

void StepSequencerUI::NoteEditor::zoomBy(int deltaNotes) {
    const int next = juce::jlimit(MIN_VISIBLE_NOTES, MAX_VISIBLE_NOTES, visibleNotes_ + deltaNotes);
    if (next == visibleNotes_)
        return;
    const int centre = lowNote_ + visibleNotes_ / 2;
    visibleNotes_ = next;
    lowNote_ = juce::jlimit(0, 128 - visibleNotes_, centre - visibleNotes_ / 2);
    repaint();
}

// =============================================================================
// Mouse interaction
// =============================================================================

void StepSequencerUI::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    const int count = pattern_.playingLength();

    // Over the steps' settings row or ruler, the menu holds the settings that have no box.
    if (e.mods.isPopupMenu() && pos.y < rulerArea_.getBottom()) {
        showSettingsMenu();
        return;
    }

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
        } else if (!keyboard_.isVisible() && !keyboardLent()) {
            openNotePicker(step);
        }
        redraw();
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
    redraw();
}

void StepSequencerUI::mouseDrag(const juce::MouseEvent& e) {
    if (dragSourceStep_ < 0)
        return;

    const int step = sf::stepAt(e.x, columnsOf(noteArea_), pattern_.playingLength());
    if (step >= 0 && step != dragTargetStep_) {
        dragTargetStep_ = step;
        redraw();
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
    redraw();
}

void StepSequencerUI::setStepNote(int stepIndex, int note) {
    if (note < 0 || note > 127)
        return;
    editPattern("Set Step Note", [stepIndex, note](step_pattern::MonoPattern& p) {
        auto& step = p.steps[static_cast<size_t>(stepIndex)];
        step.noteNumber = note;
        step.octaveShift = 0;
    });
    redraw();
}

void StepSequencerUI::openNotePicker(int stepIndex) {
    juce::Component::SafePointer<StepSequencerUI> self(this);
    const auto& step = pattern_.step(stepIndex);
    auto picker = std::make_unique<sf::NotePicker>(
        step.noteNumber + step.octaveShift * 12,
        [self, stepIndex](int note) {
            if (self == nullptr)
                return false;
            const auto& held = self->pattern_.step(stepIndex);
            return held.noteNumber + held.octaveShift * 12 == note;
        },
        [self, stepIndex](int note) {
            if (self != nullptr)
                self->setStepNote(stepIndex, note);
        });
    sf::NotePicker::show(
        *this,
        sf::cellBounds(columnsOf(noteArea_), stepIndex, pattern_.playingLength()).toNearestInt(),
        std::move(picker));
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
        safeThis->redraw();
    });
}

// =============================================================================
// Settings menu
// =============================================================================

void StepSequencerUI::showSettingsMenu() {
    using Safe = juce::Component::SafePointer<StepSequencerUI>;
    juce::PopupMenu menu;
    menu.addCustomItem(1, std::make_unique<sf::ValueMenuItem>(
                              "Quantize", [safe = Safe(this)](LinkableTextSlider& box) {
                                  box.setRange(0.0, 1.0, 0.01);
                                  box.setValueFormatter([](double v) {
                                      return juce::String(juce::roundToInt(v * 100.0)) + "%";
                                  });
                                  box.setValueParser([](const juce::String& t) {
                                      return t.trimCharactersAtEnd("%").getDoubleValue() / 100.0;
                                  });
                                  box.setValue(safe->quantize_, juce::dontSendNotification);
                                  box.onValueChanged = [safe](double v) {
                                      if (safe == nullptr)
                                          return;
                                      safe->quantize_ = v;
                                      safe->settingsEdited();
                                  };
                              }));
    menu.addCustomItem(
        2, std::make_unique<sf::ValueMenuItem>("Quantize Sub", [safe = Safe(this)](
                                                                   LinkableTextSlider& box) {
            box.setRange(16, 512, 16);
            box.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
            box.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
            box.setValue(safe->quantizeSub_, juce::dontSendNotification);
            box.onValueChanged = [safe](double v) {
                if (safe == nullptr)
                    return;
                safe->quantizeSub_ = juce::roundToInt(v);
                safe->settingsEdited();
            };
        }));
    const auto velocityItem = [this](const juce::String& caption, int& value, int slot) {
        return std::make_unique<sf::ValueMenuItem>(
            caption, [safe = juce::Component::SafePointer(this), &value, slot](auto& box) {
                box.setRange(1.0, 127.0, 1.0);
                box.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
                box.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
                box.setValue(value, juce::dontSendNotification);
                box.onValueChanged = [safe, &value, slot](double v) {
                    if (safe == nullptr)
                        return;
                    value = juce::roundToInt(v);
                    safe->sendChange(slot, static_cast<float>(value));
                };
            });
    };
    menu.addSeparator();
    menu.addCustomItem(3, velocityItem("Velocity", velocity_, SeqPlugin::kNormalVelocity));
    menu.addCustomItem(
        4, velocityItem("Accent Velocity", accentVelocity_, SeqPlugin::kAccentVelocity));
    menu.showMenuAsync(juce::PopupMenu::Options());
}

std::vector<LinkableTextSlider*> StepSequencerUI::getLinkableSliders() {
    magda::ChainNodePath dummy;
    const std::pair<LinkableTextSlider*, int> linked[] = {
        {&strip_.rate, SeqPlugin::kRate},
        {&strip_.gate, SeqPlugin::kGateLength},
        {&timeBend_.amountSlider(), SeqPlugin::kBendAmount},
        {&timeBend_.cyclesSlider(), SeqPlugin::kBendCycles}};
    std::vector<LinkableTextSlider*> sliders;
    for (const auto& [slider, slot] : linked) {
        slider->setLinkContext(magda::INVALID_DEVICE_ID, slot, dummy);
        sliders.push_back(slider);
    }
    return sliders;
}

}  // namespace magda::daw::ui
