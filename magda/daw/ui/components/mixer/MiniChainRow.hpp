#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/ChainNodePath.hpp"
#include "core/TypeIds.hpp"

namespace magda {

class AudioEngine;
class SvgButton;
namespace daw::ui {
class TextSlider;
}
namespace te = tracktion;

/**
 * @brief Compact one-device row for the mini FX chain on a mixer strip.
 *
 * Collapsed: bypass dot + device name + chevron, fixed 18px tall.
 * Expanded (planned phase 3.1): adds N small parameter sliders below.
 *
 * Click the bypass dot to toggle bypass. Click the rest of the row to
 * toggle expansion (onExpandChanged fires so the strip can relayout to
 * the new preferred height).
 */
class MiniChainRow : public juce::Component, private juce::Timer {
  public:
    static constexpr int kCollapsedHeight = 18;
    static constexpr int kParamRowHeight = 20;
    static constexpr int kMaxExpandedParams = 3;

    MiniChainRow();
    ~MiniChainRow() override;

    // Bind the row to a device on a track. Re-callable when device state
    // changes (refresh on trackDevicesChanged).
    void setDevice(const ChainNodePath& devicePath, AudioEngine* engine, const juce::String& name,
                   bool bypassed);

    void setExpanded(bool expanded);
    bool isExpanded() const {
        return expanded_;
    }

    // Sync the bypass dot to the authoritative device state without rebuilding
    // the row (used when bypass is toggled elsewhere, e.g. the device slot).
    void setBypassedState(bool bypassed);

    // Reflect the plugin editor window's open state on the "open editor" icon
    // (e.g. so it un-engages when the window is closed via its X button).
    void setPluginEditorOpen(bool open);

    DeviceId deviceId() const {
        return devicePath_.getDeviceId();
    }

    const ChainNodePath& devicePath() const {
        return devicePath_;
    }

    int preferredHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // Fires after expansion state changes so the parent strip can relayout.
    std::function<void()> onExpandChanged;

  private:
    ChainNodePath devicePath_;
    AudioEngine* engine_ = nullptr;
    juce::String deviceName_;
    bool bypassed_ = false;
    bool expanded_ = false;
    bool retainExpandedForFadeOut_ = false;
    bool paramsFadeActive_ = false;
    float paramsAlpha_ = 1.0f;
    float paramsFadeStartAlpha_ = 1.0f;
    float paramsFadeTargetAlpha_ = 1.0f;
    double paramsFadeStartMs_ = 0.0;

    juce::Rectangle<int> bypassRect_;
    juce::Rectangle<int> nameRect_;
    juce::Rectangle<int> chevronRect_;

    // "Open native editor" icon in the collapsed header (top-level non-analysis
    // devices only). Toggles the plugin window via the audio bridge.
    std::unique_ptr<SvgButton> uiButton_;

    // Up to kMaxExpandedParams parameter sliders shown when expanded. Built
    // lazily on first expand. paramLabels_ holds the corresponding name on
    // the left; paramSliders_ holds the slider on the right.
    std::vector<std::unique_ptr<daw::ui::TextSlider>> paramSliders_;
    std::vector<std::unique_ptr<juce::Label>> paramLabels_;
    // Device parameter indices (ParameterInfo::paramIndex) surfaced as rows.
    // Values are read/written in display units through the device model so
    // Faust devices (whose live param is normalized) stay in sync.
    std::vector<int> trackedParamIndices_;
    bool paramsResolved_ = false;

    void resolveParams();
    bool isParamsLaidOut() const {
        return expanded_ || retainExpandedForFadeOut_;
    }
    void startParamsFade(bool expanding);
    void advanceParamsFade();
    void applyParamsAlpha();
    void updateTimerState();
    void timerCallback() override;

    static constexpr int kParamsFadeMs = 450;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniChainRow)
};

}  // namespace magda
