// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

#include "../components/common/SvgButton.hpp"
#include "core/Config.hpp"

namespace magda {

/** @brief The three track width buttons on a view's left rail; a right-click edits them. */
class TrackWidthPresetButtons final : public juce::Component {
  public:
    TrackWidthPresetButtons();

    /** @brief Where the stack sits in a rail, above the rail's bottom group of toggles. */
    static juce::Rectangle<int> boundsInRail(juce::Rectangle<int> rail);

    std::function<void(int width)> onWidthChosen;

    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

  private:
    static constexpr int NUM_PRESETS = 3;
    static constexpr int BUTTON_HEIGHT = 16;

    void setupButton(size_t index, const juce::String& name, const char* svgData, size_t svgSize,
                     const juce::String& tooltip, int (Config::*preset)() const);

    std::array<std::unique_ptr<SvgButton>, NUM_PRESETS> buttons_;
    bool rightClicked_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackWidthPresetButtons)
};

}  // namespace magda
