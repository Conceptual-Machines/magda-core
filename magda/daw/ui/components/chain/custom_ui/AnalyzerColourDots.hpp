// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda::daw::ui {

/** @brief The analyzers' trace colours as a row of dots; a click picks one, the current one is
 *  ringed (#3187). */
class AnalyzerColourDots : public juce::Component, public juce::TooltipClient {
  public:
    void setSelectedIndex(int index);
    int getSelectedIndex() const {
        return selected_;
    }
    int getPreferredWidth() const;

    std::function<void(int index)> onChange;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

  private:
    int indexAt(juce::Point<int> point) const;
    juce::Rectangle<float> dotBounds(int index) const;

    int selected_ = 0;
    int hovered_ = -1;
};

}  // namespace magda::daw::ui
