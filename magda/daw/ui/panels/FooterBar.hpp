#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "../components/common/SvgButton.hpp"
#include "../utils/ComponentManager.hpp"
#include "core/ViewModeController.hpp"
#include "core/ViewModeState.hpp"

namespace magda {

/**
 * @brief Footer bar with view mode buttons
 *
 * Displays three icon buttons (Live/Arrange/Mix) to switch between
 * different view modes. The active mode is highlighted.
 */
class FooterBar : public juce::Component, public ViewModeListener {
  public:
    FooterBar();
    ~FooterBar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ViewModeListener interface
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    // Bottom panel collapse control
    void setBottomPanelCollapsed(bool collapsed);
    std::function<void()> onBottomPanelCollapseToggle;

  private:
    static constexpr int NUM_MODES = 3;
    static constexpr int BUTTON_SIZE = 28;
    static constexpr int BUTTON_SPACING = 16;

    std::array<magda::ManagedChild<SvgButton>, NUM_MODES> modeButtons;
    std::unique_ptr<SvgButton> bottomCollapseButton_;
    bool bottomCollapsed_ = false;

    void setupButtons();
    void setupBottomCollapseButton();
    void updateBottomCollapseIcon();
    void updateButtonStates();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FooterBar)
};

}  // namespace magda
