// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace magda {

class SvgButton;

/** @brief The 36px icon rail down the left of a clip editor (#3013). */
class EditorRail : public juce::Component {
  public:
    static constexpr int kWidth = 36;
    static constexpr int kButton = 28;

    /** @brief Rail button: secondary icon, primary on hover, an accent chip when active. */
    static void styleButton(SvgButton& button);
    /** @brief The monitor toggle: speaker icon on a green chip while @p on. */
    static void syncMonitorToggle(SvgButton& button, bool on);

    EditorRail() = default;

    /** Takes the buttons as children. @p topGroups stack down from the top, a wider gap between
     *  groups; @p bottom stacks up from the bottom edge. Hidden buttons take no space. */
    void setButtons(std::vector<std::vector<SvgButton*>> topGroups,
                    std::vector<SvgButton*> bottom = {});

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    std::vector<std::vector<SvgButton*>> topGroups_;
    std::vector<SvgButton*> bottom_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorRail)
};

}  // namespace magda
