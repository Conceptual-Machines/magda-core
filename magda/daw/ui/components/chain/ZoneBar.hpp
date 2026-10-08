#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/ChainZones.hpp"

namespace magda::daw::ui {

/**
 * @brief One of a chain's zones as a bar, dragged at its ends; in fade mode, its crossfades.
 *
 * With shoulder fades on, the bar's top corners drag the crossfades without a fade mode.
 * Shared by Drum Grid pad layers and rack chains (#1808, #3007).
 */
class ZoneBar : public juce::Component, public juce::SettableTooltipClient {
  public:
    enum class Axis { Key, Velocity, Selector };

    std::function<void(const magda::ChainZones&)> onChanged;  // during a drag
    std::function<void(const magda::ChainZones&)> onCommit;   // at its end

    void setZones(const magda::ChainZones& zones);
    void setAxis(Axis axis);
    void setFadeMode(bool fade);
    void setShoulderFades(bool shoulders);

    /// Where the rack's selector sits, drawn as a marker in Selector; negative hides it.
    void setMarker(float value);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

  private:
    enum class Grab { Low, High, Both, FadeLow, FadeHigh };

    struct Fields {
        int magda::ChainZones::*low;
        int magda::ChainZones::*high;
        int magda::ChainZones::*fadeLow;
        int magda::ChainZones::*fadeHigh;
        int min;
        int max;
    };
    Fields fields() const;
    juce::String label(int value) const;
    float xFor(float value) const;
    int valueAt(float x) const;
    void updateTooltip();

    magda::ChainZones zones_;
    magda::ChainZones start_;
    Axis axis_ = Axis::Velocity;
    Grab grabbed_ = Grab::Low;
    float marker_ = -1.0f;
    bool fade_ = false;
    bool shoulders_ = false;
    bool dragging_ = false;
};

}  // namespace magda::daw::ui
