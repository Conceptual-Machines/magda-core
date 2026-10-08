#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace magda {

class EditToolButtons;
class SvgButton;

/**
 * @brief The arrangement's v1 toolbar: edit tools centred, view button groups at the right.
 *
 * Matches the MIDI editor's toolbar shell (#3013).
 */
class ArrangementToolbar : public juce::Component {
  public:
    ArrangementToolbar();
    ~ArrangementToolbar() override;

    /** Takes @p groups as children, laid out right to left in wells split by dividers. */
    void setViewGroups(std::vector<std::vector<SvgButton*>> groups);

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    std::unique_ptr<EditToolButtons> tools_;
    std::vector<std::vector<SvgButton*>> viewGroups_;
    std::vector<juce::Rectangle<int>> wells_;
    std::vector<int> dividers_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArrangementToolbar)
};

}  // namespace magda
