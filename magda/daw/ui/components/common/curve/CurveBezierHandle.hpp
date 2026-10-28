// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "CurveTypes.hpp"

namespace magda {

class CurvePointComponent;

/**
 * @brief Draggable bezier handle for curve control
 *
 * Connected to a parent point by a line. Dragging adjusts the curve shape.
 * When the parent handle is "linked", moving this handle mirrors the opposite handle.
 */
class CurveBezierHandle : public juce::Component {
  public:
    enum class HandleType { In, Out };

    CurveBezierHandle(HandleType type, CurvePointComponent* parentPoint);
    ~CurveBezierHandle() override;

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
    void updateFromHandle(const CurveBezierHandle::HandleType type, double x, double y,
                          bool linked);
    CurveBezierHandle& getHandle() {
        return *this;
    }
    double getX() const {
        return handleX_;
    }
    double getY() const {
        return handleY_;
    }
    bool isLinked() const {
        return linked_;
    }

    // Visual state
    bool isHovered() const {
        return isHovered_;
    }

    // Callbacks
    std::function<void(HandleType, double x, double y, bool linked)> onHandleChanged;
    std::function<void(HandleType, double x, double y, bool linked)> onHandleDragPreview;

    // Size
    static constexpr int HANDLE_SIZE = 6;
    static constexpr int HIT_SIZE = 12;

  private:
    HandleType handleType_;
    CurvePointComponent* parentPoint_;
    double handleX_ = 0.0;
    double handleY_ = 0.0;
    bool linked_ = true;

    bool isDragging_ = false;
    bool isHovered_ = false;
    juce::Point<int> dragStartPos_;
    double dragStartX_ = 0.0;
    double dragStartY_ = 0.0;
};

}  // namespace magda
