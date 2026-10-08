#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/RackInfo.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/** @brief One of a pad's layers as the chain area lists it (#3007). */
struct PadLayerView {
    magda::ChainId id = magda::INVALID_CHAIN_ID;
    juce::String name;
    float volume = 0.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    bool bypassed = false;
    magda::ChainZones zones;
};

/**
 * @brief A pad layer in the rack chain-row style: [dot Name] [Gain] [Pan] [M][S][Power][x].
 */
class PadLayerRow : public juce::Component {
  public:
    PadLayerRow();
    ~PadLayerRow() override;

    void setLayer(const PadLayerView& layer, int index, bool selected, bool removable);
    magda::ChainId getLayerId() const {
        return layer_.id;
    }

    std::function<void(magda::ChainId)> onSelect;
    /// (layer, gainDb, pan) on every move; the drag's end arrives as onGestureEnd.
    std::function<void(magda::ChainId, float, float)> onMixChanged;
    std::function<void()> onGestureEnd;
    std::function<void(magda::ChainId, bool mute, bool solo, bool bypassed)> onSwitchesChanged;
    std::function<void(magda::ChainId)> onRemove;

    static constexpr int kHeight = 30;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void lookAndFeelChanged() override;

  private:
    void styleControls();
    void reportSwitches();

    PadLayerView layer_;
    int index_ = 0;
    bool selected_ = false;
    bool hovered_ = false;
    bool dragging_ = false;

    magda::DraggableValueLabel gainLabel_{magda::DraggableValueLabel::Format::Decibels};
    magda::DraggableValueLabel panLabel_{magda::DraggableValueLabel::Format::Pan};
    magda::SvgButton muteButton_{"mute", BinaryData::master_on_svg, BinaryData::master_on_svgSize};
    magda::SvgButton soloButton_{"solo", BinaryData::solo_svg, BinaryData::solo_svgSize};
    magda::SvgButton powerButton_{"Power", BinaryData::power_svg, BinaryData::power_svgSize};
    magda::SvgButton removeButton_{"Close", BinaryData::close_svg, BinaryData::close_svgSize};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadLayerRow)
};

}  // namespace magda::daw::ui
