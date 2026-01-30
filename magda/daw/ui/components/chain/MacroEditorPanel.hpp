#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/MacroInfo.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Panel for editing macro settings
 *
 * Shows when a macro is selected from the macros panel.
 * Displays name, value control, and target info.
 *
 * Layout:
 * +------------------+
 * |   MACRO NAME     |  <- Header with macro name (editable)
 * +------------------+
 * |   Value: <value> |  <- Value slider
 * +------------------+
 * | Target: Device   |  <- Target info
 * |   Param Name     |
 * +------------------+
 */
class MacroEditorPanel : public juce::Component {
  public:
    MacroEditorPanel();
    ~MacroEditorPanel() override = default;

    // Set the macro to edit
    void setMacroInfo(const magda::MacroInfo& macro);

    // Set the selected macro index (-1 for none)
    void setSelectedMacroIndex(int index);
    int getSelectedMacroIndex() const {
        return selectedMacroIndex_;
    }

    // Callbacks
    std::function<void(juce::String name)> onNameChanged;
    std::function<void(float value)> onValueChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Preferred width for this panel
    static constexpr int PREFERRED_WIDTH = 120;

  private:
    int selectedMacroIndex_ = -1;
    magda::MacroInfo currentMacro_;

    // UI Components
    juce::Label nameLabel_;
    TextSlider valueSlider_{TextSlider::Format::Decimal};
    juce::Label targetLabel_;

    void updateFromMacro();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroEditorPanel)
};

}  // namespace magda::daw::ui
