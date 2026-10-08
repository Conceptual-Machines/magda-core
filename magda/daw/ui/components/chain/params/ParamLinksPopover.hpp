#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "params/ParamLinkResolver.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"

namespace magda::daw::ui {

/**
 * @brief What modulates one parameter, opened by holding Alt over its cell.
 *
 * A bar shows the base value and the range the links can swing it across; below it, one row
 * per link: its source, amount, unipolar or bipolar, and remove.
 */
class ParamLinksPopover : public juce::Component {
  public:
    /// @p context re-reads the parameter's links, since an edit here changes them.
    ParamLinksPopover(juce::String paramName, std::function<ParamLinkContext()> context,
                      std::function<float()> baseValue);
    ~ParamLinksPopover() override;

    /// Rebuilds the rows from the parameter's current links; false when it has none left.
    bool refresh();
    int getPreferredHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

    /// Asked to close: Esc, or the mouse leaving it with Alt up.
    std::function<void()> onDismiss;

  private:
    struct Row;

    juce::String paramName_;
    std::function<ParamLinkContext()> context_;
    std::function<float()> baseValue_;
    std::vector<std::unique_ptr<Row>> rows_;

    static constexpr int WIDTH = 220;
    static constexpr int TITLE_HEIGHT = 22;
    static constexpr int RANGE_HEIGHT = 18;
    static constexpr int ROW_HEIGHT = 24;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamLinksPopover)
};

}  // namespace magda::daw::ui
