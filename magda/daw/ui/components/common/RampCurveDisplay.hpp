#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda::daw::ui {

/**
 * @brief Reusable ramp/timing curve editor.
 *
 * Draws a bezier-like curve controlled by depth (amplitude) and skew (shape).
 * The handle can be dragged interactively. Double-click resets to defaults.
 * Works at any size — the curve is always drawn within the component bounds.
 */
class RampCurveDisplay : public juce::Component, public juce::SettableTooltipClient {
  public:
    RampCurveDisplay() = default;

    void setValues(float depth, float skew) {
        if (std::abs(depth_ - depth) > 0.001f || std::abs(skew_ - skew) > 0.001f) {
            depth_ = depth;
            skew_ = skew;
            repaint();
        }
    }

    float getDepth() const {
        return depth_;
    }
    float getSkew() const {
        return skew_;
    }

    /** Called whenever depth or skew change via mouse interaction. */
    std::function<void(float depth, float skew)> onCurveChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

  private:
    float depth_ = 0.0f;
    float skew_ = 0.0f;
    float handleOffsetX_ = 0.0f;
    float handleOffsetY_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RampCurveDisplay)
};

}  // namespace magda::daw::ui
