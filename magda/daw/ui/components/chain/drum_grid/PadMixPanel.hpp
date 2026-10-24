#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/** @brief The selected pad's mix as the Drum Grid shows it beside the pad's chain. */
struct PadMixView {
    juce::String name;
    juce::String note;
    int colourIndex = 0;
    float volume = 0.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    bool bypassed = false;
};

/**
 * @brief A column for the selected pad: its name and note over gain, pan, M, S and power,
 *        which are its channel's mix (#3144).
 */
class PadMixPanel : public juce::Component {
  public:
    PadMixPanel();

    void setPad(const PadMixView& pad);
    /// An empty pad shows its name and note only: there is nothing to mix yet.
    void setMixable(bool mixable);

    /// (gainDb, pan) on every move; a drag's end arrives as onGestureEnd.
    std::function<void(float, float)> onMixChanged;
    std::function<void()> onGestureEnd;
    std::function<void(bool mute, bool solo, bool bypassed)> onSwitchesChanged;

    static constexpr int kWidth = 300;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

  private:
    void styleControls();
    void reportSwitches();

    PadMixView pad_;
    bool mixable_ = false;
    bool dragging_ = false;
    juce::Rectangle<int> nameArea_, noteArea_, gainCaption_, panCaption_;

    magda::DraggableValueLabel gainLabel_{magda::DraggableValueLabel::Format::Decibels};
    magda::DraggableValueLabel panLabel_{magda::DraggableValueLabel::Format::Pan};
    magda::SvgButton muteButton_{"mute", BinaryData::master_on_svg, BinaryData::master_on_svgSize};
    magda::SvgButton soloButton_{"solo", BinaryData::solo_svg, BinaryData::solo_svgSize};
    magda::SvgButton powerButton_{"Power", BinaryData::power_svg, BinaryData::power_svgSize};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadMixPanel)
};

}  // namespace magda::daw::ui
