#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/MacroInfo.hpp"
#include "core/TypeIds.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Scrollable content component for macro link matrix
 *
 * Displays all parameter links for the selected macro.
 * Each row: param_name | amount | delete button
 */
class MacroLinkMatrixContent : public juce::Component {
  public:
    static constexpr int ROW_HEIGHT = 18;

    struct LinkRow {
        magda::MacroTarget target;
        juce::String paramName;
        float amount = 1.0f;
        bool bipolar = false;
    };

    void setLinks(const std::vector<LinkRow>& links);

    std::function<void(magda::MacroTarget)> onDeleteLink;
    std::function<void(magda::MacroTarget, float)> onAmountChanged;
    std::function<void(magda::MacroTarget, bool)> onToggleBipolar;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    std::vector<LinkRow> links_;
    int draggingRow_ = -1;
    float dragStartAmount_ = 0.0f;
    int dragStartX_ = 0;
};

/**
 * @brief Panel for editing macro settings
 *
 * Shows when a macro is selected from the macros panel.
 * Displays name, value control, and per-link amount controls.
 *
 * Layout:
 * +------------------+
 * |   MACRO NAME     |  <- Header with macro name (editable)
 * +------------------+
 * |   Value: 0.50    |  <- Value slider
 * +------------------+
 * | Links:           |  <- Section header
 * | [param] [100%] x |  <- Per-link row (drag amount, click x to remove)
 * | [param] [ 50%] x |
 * +------------------+
 */
class MacroEditorPanel : public juce::Component {
  public:
    MacroEditorPanel();
    ~MacroEditorPanel() override = default;

    // Set the macro to edit
    void setMacroInfo(const magda::MacroInfo& macro);

    // Set a resolver for getting parameter names from device/param IDs
    void setParamNameResolver(
        std::function<juce::String(magda::DeviceId deviceId, int paramIndex)> resolver) {
        paramNameResolver_ = std::move(resolver);
    }

    // Set the selected macro index (-1 for none)
    void setSelectedMacroIndex(int index);
    int getSelectedMacroIndex() const {
        return selectedMacroIndex_;
    }

    // Callbacks
    std::function<void(juce::String name)> onNameChanged;
    std::function<void(float value)> onValueChanged;
    std::function<void(magda::MacroTarget, float amount)> onLinkAmountChanged;
    std::function<void(magda::MacroTarget)> onLinkRemoved;
    std::function<void(magda::MacroTarget, bool bipolar)> onLinkBipolarToggled;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Preferred width for this panel
    static constexpr int PREFERRED_WIDTH = 150;

  private:
    int selectedMacroIndex_ = -1;
    magda::MacroInfo currentMacro_;

    // UI Components
    juce::Label nameLabel_;
    TextSlider valueSlider_{TextSlider::Format::Decimal};
    juce::Viewport linkMatrixViewport_;
    MacroLinkMatrixContent linkMatrixContent_;

    void updateFromMacro();

    std::function<juce::String(magda::DeviceId, int)> paramNameResolver_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroEditorPanel)
};

}  // namespace magda::daw::ui
