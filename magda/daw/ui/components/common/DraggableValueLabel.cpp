#include "DraggableValueLabel.hpp"

#include <cmath>
#include <cstdio>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda {

DraggableValueLabel::DraggableValueLabel(Format format) : format_(format) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

DraggableValueLabel::~DraggableValueLabel() {
    if (editor_) {
        editor_ = nullptr;
    }
}

void DraggableValueLabel::setRange(double min, double max, double defaultValue) {
    minValue_ = min;
    maxValue_ = max;
    defaultValue_ = juce::jlimit(min, max, defaultValue);
    value_ = juce::jlimit(minValue_, maxValue_, value_);
    repaint();
}

void DraggableValueLabel::setValue(double newValue, juce::NotificationType notification) {
    newValue = juce::jlimit(minValue_, maxValue_, newValue);
    if (std::abs(newValue - value_) > 0.0001) {
        value_ = newValue;
        repaint();
        if (notification != juce::dontSendNotification && onValueChange) {
            onValueChange();
        }
    }
}

juce::String DraggableValueLabel::formatValue(double val) const {
    switch (format_) {
        case Format::Decibels: {
            if (val <= minValue_ + 0.01) {
                return "-inf";
            }
            // Convert linear to dB if needed, or just format if already dB
            juce::String sign = val >= 0 ? "+" : "";
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
            return juce::String(static_cast<int>(std::round(val)));
        }

        case Format::MidiNote: {
            // Convert MIDI note number to note name (e.g., 60 -> C4)
            int noteNumber = static_cast<int>(std::round(val));
            noteNumber = juce::jlimit(0, 127, noteNumber);
            static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                              "F#", "G",  "G#", "A",  "A#", "B"};
            int octave = (noteNumber / 12) - 1;
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
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(bounds, 2.0f);

    // Fill indicator
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));

    if (format_ == Format::Pan) {
        // Pan: draw from center outward
        float centerX = bounds.getCentreX();
        float normalizedPan = static_cast<float>(value_);  // -1 to +1

        if (std::abs(normalizedPan) < 0.01f) {
            // Center: draw thin line
            g.fillRect(centerX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
        } else if (normalizedPan < 0) {
            // Left: draw from center to left
            float fillWidth = centerX * (-normalizedPan);
            g.fillRect(centerX - fillWidth, bounds.getY(), fillWidth, bounds.getHeight());
        } else {
            // Right: draw from center to right
            float fillWidth = (bounds.getWidth() - centerX) * normalizedPan;
            g.fillRect(centerX, bounds.getY(), fillWidth, bounds.getHeight());
        }
    } else {
        // Other formats: fill from left based on normalized value
        double normalizedValue = (value_ - minValue_) / (maxValue_ - minValue_);
        normalizedValue = juce::jlimit(0.0, 1.0, normalizedValue);

        if (normalizedValue > 0.0) {
            float fillWidth = static_cast<float>(bounds.getWidth() * normalizedValue);
            auto fillBounds = bounds.withWidth(fillWidth);
            g.fillRoundedRectangle(fillBounds, 2.0f);
        }
    }

    // Border
    g.setColour(isDragging_ ? DarkTheme::getColour(DarkTheme::ACCENT_BLUE)
                            : DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 2.0f, 1.0f);

    // Text
    if (!isEditing_) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText(formatValue(value_), bounds.reduced(2, 0), juce::Justification::centred, false);
    }
}

void DraggableValueLabel::mouseDown(const juce::MouseEvent& e) {
    if (isEditing_) {
        return;
    }

    isDragging_ = true;
    dragStartValue_ = value_;
    dragStartY_ = e.y;
    repaint();
}

void DraggableValueLabel::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging_) {
        return;
    }

    // Calculate delta (dragging up increases value)
    int deltaY = dragStartY_ - e.y;

    double deltaValue;
    if (format_ == Format::BarsBeats) {
        // BarsBeats: 1 beat per ~30px, shift = fine control (0.25 beats)
        double beatsPerPixel = 1.0 / 30.0;
        deltaValue = deltaY * beatsPerPixel;
        if (e.mods.isShiftDown()) {
            deltaValue *= 0.25;
        }
    } else if (snapToInteger_ && !e.mods.isShiftDown()) {
        // Integer snap mode: 1 unit per ~30px
        deltaValue = deltaY / 30.0;
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
    isDragging_ = false;
    repaint();
}

void DraggableValueLabel::mouseDoubleClick(const juce::MouseEvent& /*e*/) {
    if (doubleClickResets_) {
        setValue(defaultValue_);
    } else {
        startEditing();
    }
}

void DraggableValueLabel::mouseWheelMove(const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& wheel) {
    if (isEditing_ || !isEnabled()) {
        return;
    }

    if (format_ == Format::BarsBeats) {
        // Determine which segment the mouse is over by measuring text
        constexpr int TICKS_PER_BEAT = 480;
        auto font = FontManager::getInstance().getUIFont(10.0f);
        auto text = formatValue(value_);
        float textWidth = font.getStringWidthFloat(text);
        auto bounds = getLocalBounds().toFloat().reduced(2, 0);
        float textStartX = bounds.getX() + (bounds.getWidth() - textWidth) * 0.5f;
        float relativeX = static_cast<float>(e.x) - textStartX;

        // Find dot positions in the rendered text
        int firstDot = text.indexOfChar('.');
        int secondDot = text.indexOfChar(firstDot + 1, '.');

        float firstDotX = font.getStringWidthFloat(text.substring(0, firstDot));
        float secondDotX =
            (secondDot >= 0) ? font.getStringWidthFloat(text.substring(0, secondDot)) : textWidth;

        // Determine increment based on segment
        double increment = 0.0;
        if (relativeX < firstDotX) {
            // Bar segment
            increment = static_cast<double>(beatsPerBar_);
        } else if (relativeX < secondDotX) {
            // Beat segment
            increment = 1.0;
        } else {
            // Tick segment
            increment = 1.0 / TICKS_PER_BEAT;
        }

        double direction = (wheel.deltaY > 0) ? 1.0 : -1.0;
        setValue(value_ + increment * direction);
    } else if (snapToInteger_) {
        // Integer snap: scroll by 1, shift = 0.25
        double increment = e.mods.isShiftDown() ? 0.25 : 1.0;
        double direction = (wheel.deltaY > 0) ? 1.0 : -1.0;
        setValue(value_ + increment * direction);
    } else {
        // Default: fall back to base class behavior
        juce::Component::mouseWheelMove(e, wheel);
    }
}

void DraggableValueLabel::startEditing() {
    if (isEditing_) {
        return;
    }

    isEditing_ = true;

    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setBounds(getLocalBounds().reduced(1));
    editor_->setFont(FontManager::getInstance().getUIFont(10.0f));
    editor_->setText(formatValue(value_), false);
    editor_->selectAll();
    editor_->setJustification(juce::Justification::centred);
    editor_->setColour(juce::TextEditor::backgroundColourId,
                       DarkTheme::getColour(DarkTheme::SURFACE));
    editor_->setColour(juce::TextEditor::textColourId,
                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    editor_->setColour(juce::TextEditor::highlightColourId,
                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    editor_->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor_->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);

    editor_->onReturnKey = [this]() { finishEditing(); };
    editor_->onEscapeKey = [this]() { cancelEditing(); };
    editor_->onFocusLost = [this]() { finishEditing(); };

    addAndMakeVisible(*editor_);
    editor_->grabKeyboardFocus();
    repaint();
}

void DraggableValueLabel::finishEditing() {
    if (!isEditing_ || !editor_) {
        return;
    }

    double newValue = parseValue(editor_->getText());
    isEditing_ = false;
    editor_ = nullptr;
    setValue(newValue);
    repaint();
}

void DraggableValueLabel::cancelEditing() {
    if (!isEditing_) {
        return;
    }

    isEditing_ = false;
    editor_ = nullptr;
    repaint();
}

}  // namespace magda
