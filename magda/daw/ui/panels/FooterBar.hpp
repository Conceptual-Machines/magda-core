// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "../components/common/SvgButton.hpp"
#include "../utils/ComponentManager.hpp"
#include "InfoLine.hpp"
#include "core/ViewModeController.hpp"
#include "core/ViewModeState.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/ControllerRegistry.hpp"

namespace magda {

/**
 * @brief Footer bar with view mode buttons
 *
 * Displays three icon buttons (Live/Arrange/Mix) to switch between
 * different view modes. The active mode is highlighted. The info line sits on
 * the left; controller badges and local-model pills on the right.
 */
class FooterBar : public juce::Component,
                  public ViewModeListener,
                  private BindingRegistryListener,
                  private ControllerRegistryListener,
                  private juce::Timer,
                  private juce::ChangeListener {
  public:
    explicit FooterBar(juce::ApplicationCommandManager* commands = nullptr);
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

    // Click on a local-model pill in the footer opens AI Settings.
    std::function<void()> onLocalModelsClicked;

    /** @brief Show the Ableton Link pill while Link is on, with its peer count (#929). */
    void setAbletonLink(bool on, std::size_t peers);
    std::function<void()> onAbletonLinkClicked;

  private:
    static constexpr int NUM_MODES = 3;
    static constexpr int NUM_LOCAL_MODELS = 2;
    static constexpr int BUTTON_SIZE = 26;
    static constexpr float BUTTON_ICON_PADDING = 3.0f;
    static constexpr int BUTTON_SPACING = 16;

    std::array<magda::ManagedChild<SvgButton>, NUM_MODES> modeButtons;
    // Modes registered with ModeContentRegistry, and the way out of an exclusive one (#3077).
    std::vector<std::pair<ViewMode, std::unique_ptr<SvgButton>>> registeredModeButtons_;
    std::unique_ptr<juce::Button> exitModeButton_;
    int registeredModesDividerX_ = -1;
    void refreshRegisteredModes();
    void updateExitModeButton(ViewMode currentMode);
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    std::unique_ptr<SvgButton> bottomCollapseButton_;
    bool bottomCollapsed_ = false;

    // ----- Local model strip on the right -----
    enum class LocalModelState {
        Unavailable,
        Idle,
        Loading,
        Loaded,
    };

    std::array<std::unique_ptr<SvgButton>, NUM_LOCAL_MODELS> localModelButtons_;
    std::array<LocalModelState, NUM_LOCAL_MODELS> localModelStates_ = {
        LocalModelState::Unavailable, LocalModelState::Unavailable};
    juce::Rectangle<int> localModelStripArea_;
    bool refreshLocalModelStatus();
    void setupLocalModelButtons();

    InfoLine infoLine_;

    // ----- Enabled-controllers strip, left of the local models -----
    struct ControllerBadge {
        juce::String label;
        bool connected = false;  // live MIDI port currently available
        juce::Rectangle<int> hitArea;
    };
    std::vector<ControllerBadge> controllerBadges_;
    /// Shown left of the controllers while Link is on; connected means at least one peer.
    std::optional<ControllerBadge> linkBadge_;
    juce::Rectangle<int> controllerStripArea_;  // overall strip bounds for click hit-testing

    void refreshControllerBadges();

    // BindingRegistryListener
    void bindingRegistryChanged(BindingScope scope) override;

    // ControllerRegistryListener
    void controllerRegistryChanged() override;

    // juce::Timer — polls MIDI device list so the green dot tracks connect/disconnect.
    void timerCallback() override;
    juce::Array<juce::MidiDeviceInfo> liveInputs_;
    bool refreshLiveInputs();  // returns true when the live set changed

    // Cached active Lua script name so timerCallback can detect load/unload
    // without reading scripting_app every frame.
    juce::String lastActiveLuaScriptName_;

    void setupButtons();
    void setupBottomCollapseButton();
    void updateBottomCollapseIcon();
    void updateButtonStates();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FooterBar)
};

}  // namespace magda
