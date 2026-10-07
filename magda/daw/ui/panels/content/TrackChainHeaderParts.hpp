#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/** @brief Styles a chain header icon: one weight, no outline, and a filled pill while
 *  engaged. @p toggles makes a click flip its state. */
void applyChainHeaderIconStyle(magda::SvgButton& button, bool toggles);

/** @brief The chain header's track label: the track number in bold tabular figures,
 *  then the name. A number of zero shows the name alone (master). */
class TrackTitleLabel : public juce::Component {
  public:
    TrackTitleLabel();

    void setTrack(int number, const juce::String& name);
    void paint(juce::Graphics& g) override;

  private:
    int number_ = 0;
    juce::String name_;
};

/** @brief A chain header value field: a dark well around a value label, with a dimmed
 *  unit (gain) or an arc glyph following the value (pan). */
class HeaderValueField : public juce::Component {
  public:
    enum class Kind { Gain, Pan };

    HeaderValueField(Kind kind, magda::DraggableValueLabel& label);

    /** Re-reads the label: dims a zero gain, moves the pan pointer. */
    void refresh();
    int getPreferredWidth() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

  private:
    Kind kind_;
    magda::DraggableValueLabel& label_;
};

/** @brief Draws the dividers between the chain header's groups; takes no clicks. */
class HeaderDividers : public juce::Component {
  public:
    HeaderDividers();

    void setDividers(std::vector<int> xs);
    void paint(juce::Graphics& g) override;

  private:
    std::vector<int> xs_;
};

}  // namespace magda::daw::ui
