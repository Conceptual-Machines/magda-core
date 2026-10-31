// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "EditTool.hpp"
#include "SvgButton.hpp"

namespace magda {

/// The arrangement's tool tooltips, Pointer to Erase.
inline constexpr std::array<const char*, 5> kArrangementToolTooltips{
    "Pointer (1)",
    "Pencil: draw a MIDI clip on an empty lane (2)",
    "Slice: split a clip where you click (3)",
    "Glue: join a clip to the next one on its track (4)",
    "Erase: click or sweep over clips (5)",
};

/** @brief Opens the five tools in a popup over @p screenArea; a pick or a 1-5 key closes it. */
void showEditToolPalette(EditToolState& state, const std::array<const char*, 5>& tooltips,
                         juce::Rectangle<int> screenArea);

/** @brief Shows @p state's tool; a click opens the palette under it. */
class CurrentEditToolButton final : public SvgButton, private juce::ChangeListener {
  public:
    CurrentEditToolButton(EditToolState& state, const std::array<const char*, 5>& tooltips);
    ~CurrentEditToolButton() override;

  private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void showTool();

    EditToolState& state_;
    std::array<const char*, 5> tooltips_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurrentEditToolButton)
};

}  // namespace magda
