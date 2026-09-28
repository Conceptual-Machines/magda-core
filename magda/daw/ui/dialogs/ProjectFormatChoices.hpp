#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstddef>

namespace magda::daw::ui {

/**
 * The sample rates and bit depths a project can be set to, and the combo-box
 * plumbing for them.
 *
 * Shared because two dialogs offer the same choices for the same fields: File >
 * Project Settings sets them on the current project, and Preferences sets the
 * defaults a new project starts from. Two lists would drift, and the first
 * symptom would be a default that cannot be expressed - a rate offered on one
 * dialog and missing from the other.
 */

inline constexpr double kProjectSampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
inline constexpr int kProjectBitDepths[] = {16, 24, 32};  // 32 = 32-bit float

/// Item ids are 1-based because a JUCE ComboBox reserves 0 for "nothing chosen".
inline void fillSampleRateCombo(juce::ComboBox& combo) {
    for (size_t i = 0; i < std::size(kProjectSampleRates); ++i)
        combo.addItem(juce::String(static_cast<int>(kProjectSampleRates[i])) + " Hz",
                      static_cast<int>(i) + 1);
}

inline void fillBitDepthCombo(juce::ComboBox& combo) {
    combo.addItem("16-bit", 1);
    combo.addItem("24-bit", 2);
    combo.addItem("32-bit float", 3);
}

/// The item id showing `rate`, or 48 kHz if the project holds a rate no combo
/// offers - which a project written by another DAW can.
inline int sampleRateItemId(double rate) {
    for (size_t i = 0; i < std::size(kProjectSampleRates); ++i)
        if (std::abs(kProjectSampleRates[i] - rate) < 0.5)
            return static_cast<int>(i) + 1;
    return 2;
}

/// The item id showing `depth`, or 24-bit for anything unrecognised.
inline int bitDepthItemId(int depth) {
    for (size_t i = 0; i < std::size(kProjectBitDepths); ++i)
        if (kProjectBitDepths[i] == depth)
            return static_cast<int>(i) + 1;
    return 2;
}

inline double sampleRateForItemId(int itemId) {
    const auto index = static_cast<size_t>(
        juce::jlimit(1, static_cast<int>(std::size(kProjectSampleRates)), itemId) - 1);
    return kProjectSampleRates[index];
}

inline int bitDepthForItemId(int itemId) {
    const auto index = static_cast<size_t>(
        juce::jlimit(1, static_cast<int>(std::size(kProjectBitDepths)), itemId) - 1);
    return kProjectBitDepths[index];
}

}  // namespace magda::daw::ui
