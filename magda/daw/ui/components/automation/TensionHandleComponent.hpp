// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/TypeIds.hpp"

namespace magda {

/**
 * @brief Draggable handle for adjusting curve tension between automation points
 *
 * Appears at the midpoint of a curve segment. Dragging up/down adjusts
 * the tension from concave (-1) through linear (0) to convex (+1).
 */
class TensionHandleComponent : public juce::Component {
  public:
    TensionHandleComponent(AutomationPointId pointId);
    ~TensionHandleComponent() override = default;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    AutomationPointId getPointId() const {
        return pointId_;
    }

    void setTension(double tension) {
        tension_ = tension;
        repaint();
    }
    double getTension() const {
        return tension_;
    }

    // Callbacks
    std::function<void(AutomationPointId, double)> onTensionChanged;
    std::function<void(AutomationPointId, double)> onTensionDragPreview;

    static constexpr int HANDLE_SIZE = 10;

  private:
    AutomationPointId pointId_;
    double tension_ = 0.0;
    bool isDragging_ = false;
    bool isHovered_ = false;
    int dragStartY_ = 0;
    double dragStartTension_ = 0.0;
};

}  // namespace magda
