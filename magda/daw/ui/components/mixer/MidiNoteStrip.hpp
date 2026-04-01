#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

/**
 * @brief Vertical mini piano-roll strip showing active MIDI notes.
 *
 * Designed to replace the LevelMeter in MIDI-only device slots (e.g. Arpeggiator).
 * Each MIDI note maps to a vertical position; active notes light up as horizontal bars
 * with brightness proportional to velocity. Recent notes decay smoothly.
 */
class MidiNoteStrip : public juce::Component, private juce::Timer {
  public:
    MidiNoteStrip() = default;

    ~MidiNoteStrip() override {
        stopTimer();
    }

    /** Set a note as active (velocity > 0) or inactive (velocity == 0). */
    void setNote(int noteNumber, int velocity) {
        if (noteNumber < 0 || noteNumber > 127)
            return;

        auto& slot = notes_[static_cast<size_t>(noteNumber)];
        float target = static_cast<float>(velocity) / 127.0f;

        if (target > slot.target) {
            slot.target = target;
            slot.display = target;  // instant attack
        } else {
            slot.target = target;
            // display decays in timerCallback
        }

        if (!isTimerRunning())
            startTimerHz(30);
    }

    /** Clear a specific note. */
    void clearNote(int noteNumber) {
        setNote(noteNumber, 0);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 1.0f);

        float height = bounds.getHeight();
        if (height < 1.0f)
            return;

        float noteRange = static_cast<float>(highNote_ - lowNote_);
        if (noteRange <= 0.0f)
            return;

        auto accent = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);

        for (int n = lowNote_; n <= highNote_; ++n) {
            float level = notes_[static_cast<size_t>(n)].display;
            if (level < 0.01f)
                continue;

            // Map note to Y position (low notes at bottom, high at top)
            float normPos = static_cast<float>(n - lowNote_) / noteRange;
            float y = bounds.getBottom() - normPos * height;

            // Bar height: at least 1px, scale with strip height
            float barH = std::max(1.5f, height / noteRange);

            g.setColour(accent.withAlpha(level * 0.9f));
            g.fillRect(bounds.getX(), y - barH * 0.5f, bounds.getWidth(), barH);
        }
    }

  private:
    static constexpr int lowNote_ = 24;    // C1
    static constexpr int highNote_ = 108;  // C8

    struct NoteSlot {
        float target = 0.0f;
        float display = 0.0f;
    };
    std::array<NoteSlot, 128> notes_{};

    static constexpr float DECAY_COEFF = 0.15f;

    void timerCallback() override {
        bool anyActive = false;
        for (auto& slot : notes_) {
            if (slot.display < 0.01f && slot.target < 0.01f) {
                slot.display = 0.0f;
                continue;
            }
            // Decay toward target
            slot.display += (slot.target - slot.display) * DECAY_COEFF;
            if (slot.display < 0.01f)
                slot.display = 0.0f;
            if (slot.display > 0.0f || slot.target > 0.0f)
                anyActive = true;
        }

        repaint();

        if (!anyActive)
            stopTimer();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiNoteStrip)
};

}  // namespace magda
