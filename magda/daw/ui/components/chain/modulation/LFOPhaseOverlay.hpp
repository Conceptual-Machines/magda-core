#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ModInfo.hpp"

namespace magda {

/**
 * @brief Opaque overlay that renders curve and animated phase indicator
 *
 * Renders the full curve visualization including:
 * - Background and grid
 * - Curve from ModInfo::curvePoints
 * - Animated phase indicator
 *
 * Being opaque prevents flickering from transparent overlay repaints.
 * Click-through allows interaction with editor components on top.
 */
class LFOPhaseOverlay : public juce::Component, private juce::Timer {
  public:
    LFOPhaseOverlay();
    ~LFOPhaseOverlay() override;

    void setModInfo(const ModInfo* mod) {
        modInfo_ = mod;
    }

    void setCurveColour(juce::Colour colour) {
        curveColour_ = colour;
    }

    void setShowCrosshair(bool show) {
        showCrosshair_ = show;
    }
    bool getShowCrosshair() const {
        return showCrosshair_;
    }

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;

  private:
    void timerCallback() override;

    void paintGrid(juce::Graphics& g);
    void paintCurve(juce::Graphics& g);
    void paintPhaseIndicator(juce::Graphics& g);

    // Apply tension curve interpolation
    double applyTension(double t, double tension) const;

    const ModInfo* modInfo_ = nullptr;
    juce::Colour curveColour_{0xFF6688CC};
    bool showCrosshair_ = false;
};

}  // namespace magda
