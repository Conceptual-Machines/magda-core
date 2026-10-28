// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/AutomationInfo.hpp"

namespace magda {

class AutomationPointComponent;

/**
 * @brief Draggable bezier handle for curve control
 *
 * Connected to a parent point by a line. Dragging adjusts the curve shape.
 * When the parent handle is "linked", moving this handle mirrors the opposite handle.
 */
class BezierHandleComponent : public juce::Component {
  public:
    enum class HandleType { In, Out };

    BezierHandleComponent(HandleType type, AutomationPointComponent* parentPoint);
    ~BezierHandleComponent() override;

    // Component
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Mouse interaction
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Handle state
    HandleType getHandleType() const {
        return handleType_;
    }
    void updateFromHandle(const BezierHandle& handle);
    BezierHandle getHandle() const {
        return handle_;
    }

    // Visual state
    void setVisible(bool visible);
    bool isHovered() const {
        return isHovered_;
    }

    // Callbacks
    std::function<void(HandleType, const BezierHandle&)> onHandleChanged;
    std::function<void(HandleType, const BezierHandle&)> onHandleDragPreview;

    // Size
    static constexpr int HANDLE_SIZE = 6;
    static constexpr int HIT_SIZE = 12;

  private:
    HandleType handleType_;
    AutomationPointComponent* parentPoint_;
    BezierHandle handle_;

    bool isDragging_ = false;
    bool isHovered_ = false;
    juce::Point<int> dragStartPos_;
    BezierHandle dragStartHandle_;
};

}  // namespace magda
