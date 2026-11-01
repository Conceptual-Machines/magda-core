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

/** @brief The narrow / medium / wide buttons under a master channel; a right-click edits them. */
class TrackWidthPresetButtons final : public juce::Component {
  public:
    static constexpr int HEIGHT = 20;

    TrackWidthPresetButtons();

    std::function<void(int width)> onWidthChosen;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

  private:
    juce::Rectangle<int> getBoxBounds() const;
    void setupButton(size_t index, const juce::String& name, const char* svgData, size_t svgSize,
                     const juce::String& tooltip, int (Config::*preset)() const);

    std::array<std::unique_ptr<SvgButton>, 3> buttons_;
    bool rightClicked_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackWidthPresetButtons)
};

}  // namespace magda
