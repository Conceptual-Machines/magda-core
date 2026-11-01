// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace magda::daw::ui::osc_wave {

/// The oscillator shapes the synth faceplates pick from with an icon.
enum class Shape { Sine, Triangle, Saw, Square, Noise };

/// How large a wave tile draws its icon (FM0's operators, Poly Synth's oscillators).
constexpr float kGlyphSize = 12.0f;

/// The shape's icon, black on transparent for tinting.
std::unique_ptr<juce::Drawable> loadIcon(Shape shape);

/// @p icon at the glyph size, centred in @p area, in the value text colour.
void drawIcon(juce::Graphics& g, const juce::Drawable& icon, juce::Rectangle<int> area,
              float alpha);

/**
 * @brief A menu of @p shapes with their icons and names, under @p area of @p owner.
 *
 * @p onPick gets the chosen shape's index into @p shapes.
 */
void showMenu(juce::Component& owner, juce::Rectangle<int> area, const std::vector<Shape>& shapes,
              int selected, std::function<void(int)> onPick);

}  // namespace magda::daw::ui::osc_wave
