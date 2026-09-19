#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace juce {
class AudioDeviceManager;
}

namespace magda {

class HardwareInputLevels;

/**
 * @brief A hybrid toggle button + dropdown selector for routing
 *
 * Features:
 * - Click main area to toggle enable/disable
 * - Click dropdown arrow to select routing source/destination
 * - Right-click anywhere opens the selection menu
 * - Color-coded based on routing type and enabled state
 */
class RoutingSelector : public juce::Component {
  public:
    enum class Type { AudioIn, AudioOut, MidiIn, MidiOut };

    struct RoutingOption {
        int id;
        juce::String name;
        bool isSeparator = false;

        /// Physical input channels the option reads, metered in the menu.
        std::vector<int> inputChannels = {};
    };

    RoutingSelector(Type type);
    ~RoutingSelector() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Enable/disable state ("enabled" = routing active; still interactive)
    void setEnabled(bool shouldBeEnabled);
    bool isEnabled() const {
        return enabled_;
    }

    // Read-only mode: greyed out and non-interactive (no popup / hover), used
    // when another owner controls the routing (e.g. an External Instrument
    // insert owns a track's MIDI-out / audio-in). When displayText is set it is
    // shown verbatim so the control can mirror the owner's selection; otherwise
    // it shows "None". Passing readOnly=false restores normal behaviour.
    void setReadOnly(bool readOnly, const juce::String& displayText = {});
    bool isReadOnly() const {
        return readOnly_;
    }

    // Current selection
    void setSelectedId(int id);
    int getSelectedId() const {
        return selectedId_;
    }
    juce::String getSelectedName() const;

    // Available options
    void setOptions(const std::vector<RoutingOption>& options);
    void clearOptions();

    /** Returns the ID of the first channel option (skipping "None"/separators), or -1. */
    int getFirstChannelOptionId() const;

    /// Where the menu reads the levels of options naming input channels. Null
    /// shows no meters.
    void meterInputsFrom(juce::AudioDeviceManager* devices) {
        inputDevices_ = devices;
    }

    // Callbacks
    std::function<void(bool enabled)> onEnabledChanged;
    std::function<void(int selectedId)> onSelectionChanged;

  private:
    Type type_;
    bool enabled_ = true;
    bool isHovering_ = false;
    bool readOnly_ = false;
    juce::String readOnlyDisplay_;
    int selectedId_ = -1;
    std::vector<RoutingOption> options_;

    juce::AudioDeviceManager* inputDevices_ = nullptr;

    /// Held while the menu is open, and released with this if the menu outlives it.
    std::shared_ptr<HardwareInputLevels> inputLevels_;

    // Layout
    static constexpr int DROPDOWN_ARROW_WIDTH = 10;

    juce::Rectangle<int> getMainButtonArea() const;
    juce::Rectangle<int> getDropdownArea() const;

    void showPopupMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingSelector)
};

}  // namespace magda
