#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "EditTool.hpp"

namespace magda {

class SvgButton;

namespace edit_toolbar {
constexpr int kHeight = 40;
constexpr int kButton = 28;
constexpr int kWellPad = 3;
constexpr int kGap = 2;

/** @brief v1 toolbar button: 28px, radius 6, a filled chip with a hairline when engaged. */
void styleButton(SvgButton& button);
/** @brief The recessed well behind a button group. */
void paintWell(juce::Graphics& g, juce::Rectangle<int> well);
/** @brief A vertical divider between toolbar groups. */
void paintDivider(juce::Graphics& g, int x, int toolbarHeight);
}  // namespace edit_toolbar

/** @brief The five tool buttons in their well, bound to one surface's EditToolState. */
class EditToolButtons : public juce::Component, private juce::ChangeListener {
  public:
    static constexpr int kWidth =
        edit_toolbar::kButton * 5 + edit_toolbar::kGap * 4 + edit_toolbar::kWellPad * 2;
    static constexpr int kHeight = edit_toolbar::kButton + edit_toolbar::kWellPad * 2;

    /** @p tooltips are in tool order, Pointer to Erase. */
    EditToolButtons(EditToolState& state, const std::array<const char*, 5>& tooltips);
    ~EditToolButtons() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void syncButtons();

    EditToolState& state_;
    std::array<std::unique_ptr<SvgButton>, 5> buttons_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditToolButtons)
};

}  // namespace magda
