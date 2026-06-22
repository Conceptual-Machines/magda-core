#include "DraggableValueLabel.hpp"

#include <cmath>
#include <cstdio>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "ValueEditGesture.hpp"
#include "core/AutomationManager.hpp"
#include "core/ParameterInfo.hpp"
#include "core/ParameterUtils.hpp"

namespace magda {

namespace {
daw::ui::ValueLabelControl::TintState toControlTintState(AutomationVisualState state) {
    switch (state) {
        case AutomationVisualState::Overridden:
            return daw::ui::ValueLabelControl::TintState::Overridden;
        case AutomationVisualState::Active:
            return daw::ui::ValueLabelControl::TintState::Automated;
        case AutomationVisualState::None:
        default:
            return daw::ui::ValueLabelControl::TintState::None;
    }
}
}  // namespace

DraggableValueLabel::DraggableValueLabel(Format format) : format_(format) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    valueControl_.onMouseDown = [this](const juce::MouseEvent& e) { mouseDown(e); };
    valueControl_.onMouseDrag = [this](const juce::MouseEvent& e) { mouseDrag(e); };
    valueControl_.onMouseUp = [this](const juce::MouseEvent& e) { mouseUp(e); };
    valueControl_.onMouseDoubleClick = [this](const juce::MouseEvent& e) { mouseDoubleClick(e); };
    valueControl_.onMouseWheel = [this](const juce::MouseEvent& e,
                                        const juce::MouseWheelDetails& wheel) {
        mouseWheelMove(e, wheel);
    };
    valueControl_.onEditCommit = [this](const juce::String& text) { finishEditing(text); };
    valueControl_.onEditCancel = [this]() { cancelEditing(); };
    addAndMakeVisible(valueControl_);
    syncValueControl();
}

DraggableValueLabel::~DraggableValueLabel() {
    if (listeningToAutomation_) {
        AutomationManager::getInstance().removeListener(this);
        listeningToAutomation_ = false;
    }
}

void DraggableValueLabel::setVertical(bool vertical) {
    vertical_ = vertical;
    syncValueControl();
}

void DraggableValueLabel::setRange(double min, double max, double defaultValue) {
    minValue_ = min;
    maxValue_ = max;
    defaultValue_ = juce::jlimit(min, max, defaultValue);
    value_ = juce::jlimit(minValue_, maxValue_, value_);
    syncValueControl();
}

void DraggableValueLabel::setValue(double newValue, juce::NotificationType notification) {
    newValue = juce::jlimit(minValue_, maxValue_, newValue);
    if (std::abs(newValue - value_) > 0.0001) {
        value_ = newValue;
        syncValueControl();
        if (notification != juce::dontSendNotification && onValueChange) {
            onValueChange();
        }
    } else {
        syncValueControl();
    }
}

juce::String DraggableValueLabel::formatValue(double val) const {
    switch (format_) {
        case Format::Decibels: {
            if (val <= minValue_ + 0.01) {
                return "-inf";
            }
            // Snap near-zero to exact zero to avoid "+0.0" / "-0.0"
            if (std::abs(val) < 0.05) {
                return "0.0";
            }
            juce::String sign = val > 0 ? "+" : "";
            return sign + juce::String(val, 1);
        }

        case Format::Pan: {
            if (std::abs(val) < 0.01) {
                return "C";
            } else if (val < 0) {
                int pct = static_cast<int>(std::round(-val * 100));
                return "L" + juce::String(pct);
            } else {
                int pct = static_cast<int>(std::round(val * 100));
                return "R" + juce::String(pct);
            }
        }

        case Format::Percentage: {
            int pct = static_cast<int>(std::round(val * 100));
            return juce::String(pct) + "%";
        }

        case Format::Integer: {
            return juce::String(static_cast<int>(std::round(val))) + suffix_;
        }

        case Format::MidiNote: {
            // Convert MIDI note number to note name (e.g., 60 -> C4)
            int noteNumber = static_cast<int>(std::round(val));
            noteNumber = juce::jlimit(0, 127, noteNumber);
            static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                              "F#", "G",  "G#", "A",  "A#", "B"};
            int octave = (noteNumber / 12) - 2;
            int noteIndex = noteNumber % 12;
            return juce::String(noteNames[noteIndex]) + juce::String(octave);
        }

        case Format::Beats: {
            return juce::String(val, 2) + " beats";
        }

        case Format::BarsBeats: {
            constexpr int TICKS_PER_BEAT = 480;
            int wholeBars = static_cast<int>(val / beatsPerBar_);
            double remaining = std::fmod(val, static_cast<double>(beatsPerBar_));
            if (remaining < 0.0)
                remaining = 0.0;
            int wholeBeats = static_cast<int>(remaining);
            int ticks = static_cast<int>((remaining - wholeBeats) * TICKS_PER_BEAT);
            int offset = barsBeatsIsPosition_ ? 1 : 0;
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%d.%d.%03d", wholeBars + offset,
                          wholeBeats + offset, ticks);
            return juce::String(buffer);
        }

        case Format::Raw:
        default:
            return juce::String(val, decimalPlaces_) + suffix_;
    }
}

double DraggableValueLabel::parseValue(const juce::String& text) const {
    juce::String trimmed = text.trim().toLowerCase();

    switch (format_) {
        case Format::Decibels: {
            if (trimmed == "-inf" || trimmed == "inf" || trimmed == "-infinity") {
                return minValue_;
            }
            // Remove "db" suffix if present
            if (trimmed.endsWith("db")) {
                trimmed = trimmed.dropLastCharacters(2).trim();
            }
            return trimmed.getDoubleValue();
        }

        case Format::Pan: {
            if (trimmed == "c" || trimmed == "center" || trimmed == "0") {
                return 0.0;
            }
            if (trimmed.startsWith("l")) {
                double pct = trimmed.substring(1).getDoubleValue();
                return -pct / 100.0;
            }
            if (trimmed.startsWith("r")) {
                double pct = trimmed.substring(1).getDoubleValue();
                return pct / 100.0;
            }
            // Try parsing as number (-100 to 100)
            double val = trimmed.getDoubleValue();
            return val / 100.0;
        }

        case Format::Percentage: {
            // Remove % if present
            if (trimmed.endsWith("%")) {
                trimmed = trimmed.dropLastCharacters(1).trim();
            }
            return trimmed.getDoubleValue() / 100.0;
        }

        case Format::Integer: {
            return std::round(trimmed.getDoubleValue());
        }

        case Format::MidiNote: {
            // Parse note name (e.g., "C4", "D#5") back to MIDI note number
            if (trimmed.isEmpty()) {
                return 60.0;  // Default to middle C
            }

            // Try to parse as a MIDI note name
            static const char* noteNames[] = {"c",  "c#", "d",  "d#", "e",  "f",
                                              "f#", "g",  "g#", "a",  "a#", "b"};
            static const char* altNoteNames[] = {"c",  "db", "d",  "eb", "e",  "f",
                                                 "gb", "g",  "ab", "a",  "bb", "b"};

            int noteIndex = -1;
            int charsParsed = 0;

            // Check for sharp/flat note names first (2 chars)
            for (int i = 0; i < 12; ++i) {
                juce::String noteName(noteNames[i]);
                juce::String altName(altNoteNames[i]);
                if (trimmed.startsWith(noteName)) {
                    noteIndex = i;
                    charsParsed = noteName.length();
                    break;
                }
                if (trimmed.startsWith(altName)) {
                    noteIndex = i;
                    charsParsed = altName.length();
                    break;
                }
            }

            if (noteIndex < 0) {
                // Try parsing as a number
                return trimmed.getDoubleValue();
            }

            // Parse octave
            juce::String octaveStr = trimmed.substring(charsParsed);
            int octave = octaveStr.getIntValue();

            return static_cast<double>((octave + 1) * 12 + noteIndex);
        }

        case Format::Beats: {
            // Remove " beats" suffix if present
            if (trimmed.endsWith("beats")) {
                trimmed = trimmed.dropLastCharacters(5).trim();
            }
            return trimmed.getDoubleValue();
        }

        case Format::BarsBeats: {
            constexpr int TICKS_PER_BEAT = 480;
            int offset = barsBeatsIsPosition_ ? 1 : 0;
            auto parts = juce::StringArray::fromTokens(trimmed, ".", "");
            int bar = 0, beat = 0, ticks = 0;
            if (parts.size() >= 1)
                bar = parts[0].getIntValue() - offset;
            if (parts.size() >= 2)
                beat = parts[1].getIntValue() - offset;
            if (parts.size() >= 3)
                ticks = parts[2].getIntValue();
            if (bar < 0)
                bar = 0;
            if (beat < 0)
                beat = 0;
            if (ticks < 0)
                ticks = 0;
            return bar * beatsPerBar_ + beat + ticks / static_cast<double>(TICKS_PER_BEAT);
        }

        case Format::Raw:
        default:
            // Remove suffix if present
            if (suffix_.isNotEmpty() && trimmed.endsWith(suffix_.toLowerCase())) {
                trimmed = trimmed.dropLastCharacters(suffix_.length()).trim();
            }
            return trimmed.getDoubleValue();
    }
}

void DraggableValueLabel::paint(juce::Graphics& g) {
    juce::ignoreUnused(g);
}

void DraggableValueLabel::resized() {
    valueControl_.setBounds(getLocalBounds());
}

void DraggableValueLabel::mouseDown(const juce::MouseEvent& e) {
    if (valueControl_.isEditing()) {
        return;
    }

    if (daw::ui::isDirectValueEditGesture(e)) {
        startEditing();
        return;
    }

    if (e.mods.isPopupMenu()) {
        if (e.mods.isShiftDown())
            startEditing();
        else if (onRightClick)
            onRightClick();
        return;
    }

    isDragging_ = true;
    valueControl_.setDragging(true);
    overrideLatchedThisGesture_ = false;
    dragStartValue_ = value_;
    dragStartY_ = e.y;

    if (onDragStart)
        onDragStart();

    // Suppress playback write-back for the duration of the gesture so the
    // engine doesn't fight a drag-in-progress. This is transient (cleared on
    // mouseUp) and is NOT the same as the persistent override/bypass — that
    // only latches once we see a real value change (see latchAutomationOverride).
    if (hasAutomationTarget_ && isAutomated()) {
        AutomationManager::getInstance().setTargetTouchSuppressed(automationTarget_, true);
    }
    // Always mark the gesture on the target (even when no lane exists yet) so
    // AutomationRecordingEngine can distinguish real user touches from playback
    // engine echo-backs when deciding whether to record a point.
    if (hasAutomationTarget_) {
        AutomationManager::getInstance().setTargetUserTouched(automationTarget_, true);
        // Capture pre-drag value as the Touch-mode bounce-back baseline. Same
        // mechanism TextSlider uses; needed here for any DraggableValueLabel
        // wired to a Macro / ModParameter / device target.
        ParameterInfo info = getParameterInfoForTarget(automationTarget_);
        double normalized = static_cast<double>(
            ParameterUtils::realToNormalized(static_cast<float>(dragStartValue_), info));
        AutomationManager::getInstance().setTouchBaseline(automationTarget_, normalized);
    }

    syncValueControl();
}

void DraggableValueLabel::latchAutomationOverride() {
    if (overrideLatchedThisGesture_)
        return;
    if (!hasAutomationTarget_ || !isAutomated())
        return;
    // Write mode is ACTIVELY recording into the lane — do not bypass it.
    if (AutomationManager::getInstance().isWriteModeEnabled())
        return;
    auto& mgr = AutomationManager::getInstance();
    mgr.setTargetOverridden(automationTarget_, true);
    setAutomationVisualState(AutomationVisualState::Overridden);
    overrideLatchedThisGesture_ = true;
}

void DraggableValueLabel::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_) {
        return;
    }

    // Calculate delta (dragging up increases value)
    int deltaY = dragStartY_ - e.y;

    // Confirm gesture as a real edit once the drag crosses a small threshold —
    // only then do we latch the persistent override. A plain click or an
    // aborted gesture never reaches this point.
    constexpr int kDragThresholdPx = 2;
    if (std::abs(deltaY) >= kDragThresholdPx)
        latchAutomationOverride();

    double deltaValue;
    if (format_ == Format::BarsBeats) {
        // BarsBeats: 1 beat per ~30px, shift = fine control (0.25 beats)
        double beatsPerPixel = 1.0 / 30.0;
        deltaValue = deltaY * beatsPerPixel;
        if (e.mods.isShiftDown()) {
            deltaValue *= 0.25;
        }
    } else if (snapToInteger_ && !e.mods.isShiftDown()) {
        // Integer snap mode: 1 unit per ~10px
        deltaValue = deltaY / 10.0;
        double newValue = std::round(dragStartValue_ + deltaValue);
        setValue(newValue);
        return;
    } else {
        double range = maxValue_ - minValue_;
        deltaValue = (deltaY / dragSensitivity_) * range;

        // Fine control with shift key
        if (e.mods.isShiftDown()) {
            deltaValue *= 0.1;
        }
    }

    setValue(dragStartValue_ + deltaValue);
}

void DraggableValueLabel::mouseUp(const juce::MouseEvent& /*e*/) {
    bool wasDragging = isDragging_;
    isDragging_ = false;
    valueControl_.setDragging(false);

    // Release the transient flags; the lane stays in bypass (override) state
    // until the user explicitly re-enables it from the lane header.
    if (hasAutomationTarget_) {
        auto& mgr = AutomationManager::getInstance();
        mgr.setTargetUserTouched(automationTarget_, false);
        mgr.setTargetTouchSuppressed(automationTarget_, false);
        mgr.clearTouchBaseline(automationTarget_);
    }

    syncValueControl();
    if (wasDragging && onDragEnd)
        onDragEnd(dragStartValue_);
}

void DraggableValueLabel::mouseDoubleClick(const juce::MouseEvent& e) {
    const bool wasDragging = isDragging_;
    isDragging_ = false;
    valueControl_.setDragging(false);
    if (wasDragging && hasAutomationTarget_) {
        auto& mgr = AutomationManager::getInstance();
        mgr.setTargetUserTouched(automationTarget_, false);
        mgr.setTargetTouchSuppressed(automationTarget_, false);
        mgr.clearTouchBaseline(automationTarget_);
    }

    if (doubleClickResets_ && !e.mods.isShiftDown()) {
        if (value_ != defaultValue_)
            latchAutomationOverride();
        setValue(defaultValue_);
    } else {
        startEditing();
    }
}

void DraggableValueLabel::mouseWheelMove(const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& wheel) {
    // Don't adjust values on scroll — too easy to accidentally change
    // values when scrolling the inspector with a trackpad.
    // Let the parent handle the scroll event for viewport scrolling.
    juce::Component::mouseWheelMove(e, wheel);
}

void DraggableValueLabel::syncValueControl() {
    valueControl_.setRange(minValue_, maxValue_);
    valueControl_.setValue(value_);
    valueControl_.setDisplayText(formatValue(value_));
    if (vertical_) {
        valueControl_.setFillMode(daw::ui::ValueLabelControl::FillMode::BottomToTop);
    } else {
        valueControl_.setFillMode(format_ == Format::Pan
                                      ? daw::ui::ValueLabelControl::FillMode::PanCentre
                                      : daw::ui::ValueLabelControl::FillMode::LeftToRight);
    }
    valueControl_.setVertical(vertical_);
    valueControl_.setShowFillIndicator(showFillIndicator_);
    valueControl_.setDrawBackground(drawBackground_);
    valueControl_.setDrawBorder(drawBorder_);
    valueControl_.setFontSize(fontSize_);
    valueControl_.setJustification(justification_);
    valueControl_.setDragging(isDragging_);
    valueControl_.setCoEditing(coEditing_);
    valueControl_.setTintState(toControlTintState(automationVisualState_));

    if (customTextColour_)
        valueControl_.setTextColour(*customTextColour_);
    else
        valueControl_.clearTextColour();

    if (customFillColour_)
        valueControl_.setFillColour(*customFillColour_);
    else
        valueControl_.clearFillColour();

    if (textOverride_.isNotEmpty())
        valueControl_.setTextOverride(textOverride_);
    else
        valueControl_.clearTextOverride();
}

void DraggableValueLabel::startEditing() {
    if (valueControl_.isEditing()) {
        return;
    }

    valueControl_.showEditor(formatValue(value_));
}

void DraggableValueLabel::finishEditing(const juce::String& text) {
    double newValue = parseValue(text);
    if (newValue != value_)
        latchAutomationOverride();
    setValue(newValue);
}

void DraggableValueLabel::cancelEditing() {
    if (!valueControl_.isEditing()) {
        return;
    }

    valueControl_.cancelEditing();
}

}  // namespace magda
