#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace magda {

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
    };

    RoutingSelector(Type type);
    ~RoutingSelector() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Enable/disable state
    void setEnabled(bool shouldBeEnabled);
    bool isEnabled() const {
        return enabled_;
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

    // Callbacks
    std::function<void(bool enabled)> onEnabledChanged;
    std::function<void(int selectedId)> onSelectionChanged;

  private:
    Type type_;
    bool enabled_ = true;
    bool isHovering_ = false;
    int selectedId_ = -1;
    std::vector<RoutingOption> options_;

    // Layout
    static constexpr int DROPDOWN_ARROW_WIDTH = 10;

    juce::Rectangle<int> getMainButtonArea() const;
    juce::Rectangle<int> getDropdownArea() const;

    void showPopupMenu();
    juce::Colour getEnabledColour() const;

    // Icon drawable (loaded once in constructor)
    std::unique_ptr<juce::Drawable> icon_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingSelector)
};

}  // namespace magda
