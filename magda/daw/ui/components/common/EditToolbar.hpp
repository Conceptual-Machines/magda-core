#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

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

/** @brief The wells and dividers a toolbar layout produced, for its paint(). */
struct Decor {
    std::vector<juce::Rectangle<int>> wells;
    std::vector<int> dividers;

    void clear();
    void paint(juce::Graphics& g, int toolbarHeight) const;
};

/**
 * @brief Places toolbar items in a row from one edge, vertically centred, recording wells and
 * dividers into a Decor.
 */
class Row {
  public:
    enum class From { Left, Right };

    Row(Decor& decor, juce::Rectangle<int> bounds, int startX, From from);

    /** Centres @p build's items in @p bounds; @p build runs twice, to measure then place. */
    static void centred(Decor& decor, juce::Rectangle<int> bounds,
                        const std::function<void(Row&)>& build);

    Row& button(juce::Component& component);
    Row& item(juce::Component& component, int width, int height);
    Row& space(int px);
    Row& divider();
    Row& openWell();
    Row& closeWell();

    int position() const {
        return x_;
    }

  private:
    juce::Rectangle<int> take(int width, int height);

    Decor* decor_;
    juce::Rectangle<int> bounds_;
    int x_;
    From from_;
    int wellStart_ = -1;
    bool wellHasItem_ = false;
};
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
