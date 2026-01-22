#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "MacroKnobComponent.hpp"
#include "PagedControlPanel.hpp"
#include "core/MacroInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief Paginated panel for macro knobs
 *
 * Shows 8 macros per page in a 2x4 grid with page navigation.
 * Inherits from PagedControlPanel for pagination support.
 *
 * Layout:
 * +------------------+
 * |   < Page 1/2 >   |  <- Only shown if > 8 macros
 * +------------------+
 * | [M1] [M2]        |
 * | [M3] [M4]        |
 * | [M5] [M6]        |
 * | [M7] [M8]        |
 * +------------------+
 */
class MacroPanelComponent : public PagedControlPanel {
  public:
    MacroPanelComponent();
    ~MacroPanelComponent() override = default;

    // Set macros from rack/chain data
    void setMacros(const magda::MacroArray& macros);

    // Set available devices for linking (devices in this rack/chain)
    void setAvailableDevices(const std::vector<std::pair<magda::DeviceId, juce::String>>& devices);

    // Callbacks
    std::function<void(int macroIndex, float value)> onMacroValueChanged;
    std::function<void(int macroIndex, magda::MacroTarget target)> onMacroTargetChanged;
    std::function<void(int macroIndex, juce::String name)> onMacroNameChanged;
    std::function<void(int macroIndex)> onMacroClicked;  // Selection callback

  protected:
    // PagedControlPanel overrides
    int getTotalItemCount() const override;
    juce::Component* getItemComponent(int index) override;
    juce::String getPanelTitle() const override {
        return "MACROS";
    }

  private:
    std::vector<std::unique_ptr<MacroKnobComponent>> knobs_;
    std::vector<std::pair<magda::DeviceId, juce::String>> availableDevices_;

    void ensureKnobCount(int count);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroPanelComponent)
};

}  // namespace magda::daw::ui
