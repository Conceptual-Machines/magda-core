#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "../components/common/SvgButton.hpp"
#include "../utils/ComponentManager.hpp"
#include "core/ViewModeController.hpp"
#include "core/ViewModeState.hpp"
#include "core/controllers/ControllerRegistry.hpp"

namespace magda {

/**
 * @brief Footer bar with view mode buttons
 *
 * Displays three icon buttons (Live/Arrange/Mix) to switch between
 * different view modes. The active mode is highlighted.
 */
class FooterBar : public juce::Component,
                  public ViewModeListener,
                  private ControllerRegistryListener,
                  private juce::Timer {
  public:
    FooterBar();
    ~FooterBar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;

    // ViewModeListener interface
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // Bottom panel collapse control
    void setBottomPanelCollapsed(bool collapsed);
    std::function<void()> onBottomPanelCollapseToggle;

    // Click on a controller pill in the footer opens the Controllers dialog.
    std::function<void()> onControllersClicked;

  private:
    static constexpr int NUM_MODES = 3;
    static constexpr int BUTTON_SIZE = 32;
    static constexpr int BUTTON_SPACING = 16;

    std::array<magda::ManagedChild<SvgButton>, NUM_MODES> modeButtons;
    std::unique_ptr<SvgButton> bottomCollapseButton_;
    bool bottomCollapsed_ = false;

    // ----- Enabled-controllers strip on the left -----
    struct ControllerBadge {
        juce::String label;
        bool connected = false;  // live MIDI port currently available
        juce::Rectangle<int> hitArea;
    };
    std::vector<ControllerBadge> controllerBadges_;
    juce::Rectangle<int> controllerStripArea_;  // overall strip bounds for click hit-testing

    void refreshControllerBadges();

    // ControllerRegistryListener
    void controllerRegistryChanged() override;

    // juce::Timer — polls MIDI device list so the green dot tracks connect/disconnect.
    void timerCallback() override;
    juce::Array<juce::MidiDeviceInfo> liveInputs_;
    bool refreshLiveInputs();  // returns true when the live set changed

    void setupButtons();
    void setupBottomCollapseButton();
    void updateBottomCollapseIcon();
    void updateButtonStates();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FooterBar)
};

}  // namespace magda
