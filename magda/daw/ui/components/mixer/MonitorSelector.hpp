#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "../../../core/TrackInfo.hpp"  // InputMonitorMode

namespace magda {

/**
 * @brief Input-monitor control: a speaker icon + state label + dropdown.
 *
 * Mirrors RoutingSelector's look so it reads as part of the same family of
 * track-header controls. Two interaction zones in one component:
 * - Click the speaker icon (left): cycles Off → In → Auto → Off.
 * - Click the label / dropdown arrow (right): opens a menu to pick directly.
 *
 * Both paths fire onModeChanged with the resulting mode; the owner applies it
 * (e.g. via SetTrackInputMonitorCommand) and calls setMode() to reflect state.
 */
class MonitorSelector : public juce::Component, public juce::SettableTooltipClient {
  public:
    MonitorSelector();
    ~MonitorSelector() override = default;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    void setMode(InputMonitorMode mode);
    InputMonitorMode getMode() const {
        return mode_;
    }

    // Fired when the user cycles (icon) or selects (menu) a mode.
    std::function<void(InputMonitorMode)> onModeChanged;

  private:
    InputMonitorMode mode_ = InputMonitorMode::Off;
    bool isHovering_ = false;
    std::unique_ptr<juce::Drawable> speakerIcon_;     // waved speaker (In / Auto)
    std::unique_ptr<juce::Drawable> speakerOffIcon_;  // plain no-waves speaker (Off)

    static constexpr int DROPDOWN_ARROW_WIDTH = 10;

    juce::Rectangle<int> getIconArea() const;      // clickable icon area (cycles)
    juce::Rectangle<int> getDropdownArea() const;  // right arrow (opens menu)
    bool isActiveMode() const {
        return mode_ != InputMonitorMode::Off;
    }
    static InputMonitorMode nextMode(InputMonitorMode mode);
    void showPopupMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonitorSelector)
};

}  // namespace magda
