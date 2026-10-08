#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/RackInfo.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/** @brief One of a pad's layers as the pad editor lists it (#3007). */
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
 * @brief A pad layer in one of the editor's views: [dot Name] then the view's controls.
 *
 * Mix shows gain, pan, M, S, power and remove; Velocity the layer's velocity range as a bar
 * dragged at either end, with its round-robin switch; Fade the same bar, its crossfades
 * dragged in from each end.
 */
class PadLayerRow : public juce::Component {
  public:
    enum class View { Mix, Velocity, Fade };

    PadLayerRow();
    ~PadLayerRow() override;

    void setLayer(const PadLayerView& layer, int index, bool selected);
    void setView(View view);
    magda::ChainId getLayerId() const {
        return layer_.id;
    }

    std::function<void(magda::ChainId)> onSelect;
    /// (layer, gainDb, pan) on every move; a drag's end arrives as onGestureEnd.
    std::function<void(magda::ChainId, float, float)> onMixChanged;
    std::function<void()> onGestureEnd;
    std::function<void(magda::ChainId, bool mute, bool solo, bool bypassed)> onSwitchesChanged;
    /// Once per drag, when it ends: a zone change recompiles the pad.
    std::function<void(magda::ChainId, const magda::ChainZones&)> onZonesChanged;
    std::function<void(magda::ChainId)> onRemove;

    static constexpr int kHeight = 28;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void lookAndFeelChanged() override;

  private:
    class ZoneBar;

    void styleControls();
    void reportSwitches();

    PadLayerView layer_;
    View view_ = View::Mix;
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
    juce::TextButton roundRobinButton_{"RR"};
    std::unique_ptr<ZoneBar> zoneBar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadLayerRow)
};

}  // namespace magda::daw::ui
