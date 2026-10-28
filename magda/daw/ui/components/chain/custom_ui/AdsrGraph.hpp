// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <optional>
#include <utility>

#include "core/ParameterInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief Draggable ADSR envelope display.
 *
 * Renders the classic attack / decay / sustain / release shape with three
 * drag handles (peak, sustain corner, release end). Dragging maps back to the
 * underlying host-slot display values via each stage's ParameterInfo and fires
 * onStageChanged(paramIndex, displayValue) — the owner forwards that to the
 * plugin write AND to the matching value box, so the graph and the boxes stay
 * in lockstep.
 *
 * Purely a view+gesture widget: it holds no plugin reference and does not link
 * itself. The value boxes remain the linkable/automatable controls.
 */
class AdsrGraph : public juce::Component {
  public:
    enum Stage { Attack = 0, Decay = 1, Sustain = 2, Release = 3, kNumStages = 4 };

    AdsrGraph();
    ~AdsrGraph() override = default;

    /// Bind one stage to its host slot. `info` supplies the range/scale used to
    /// map the handle position back to a real value.
    void setStage(Stage stage, int paramIndex, const magda::ParameterInfo& info,
                  float displayValue);

    /// Update just the current value of a stage (e.g. after a value-box edit).
    void setStageValue(Stage stage, float displayValue);

    /// Draw on @p background with @p line, instead of the theme's background and accent.
    void setColours(juce::Colour background, juce::Colour line) {
        colours_ = {background, line};
        repaint();
    }

    std::function<void(int paramIndex, float displayValue)> onStageChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    std::optional<std::pair<juce::Colour, juce::Colour>> colours_;

    struct StageState {
        int paramIndex = -1;
        magda::ParameterInfo info;
        float value = 0.0f;
        bool set = false;
    };

    // Geometry derived from the current values and bounds.
    struct Geometry {
        float x0 = 0, yTop = 0, yBot = 0;
        juce::Point<float> peak, sustainCorner, releaseEnd;
        float segMax = 0, sustainW = 0;
    };
    Geometry computeGeometry() const;

    // 0..1 normalized position of a stage's value within its range.
    float frac(Stage stage) const;
    void commitFrac(Stage stage, float newFrac);  // writes value + fires callback

    std::array<StageState, kNumStages> stages_;
    int dragStage_ = -1;  // which handle is being dragged (-1 = none)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdsrGraph)
};

}  // namespace magda::daw::ui
