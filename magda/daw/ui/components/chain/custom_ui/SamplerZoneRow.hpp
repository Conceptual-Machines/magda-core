#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "ZoneBar.hpp"
#include "plugins/sampler/SamplerZones.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/**
 * @brief A sampler zone in the faceplate's zone list (#3098), as a rack lists its chains.
 *
 * Mix shows the zone's gain and remove; Key and Velocity its range as a bar dragged at either
 * end, its fades at the shoulders, and in Velocity its round-robin switch.
 */
class SamplerZoneRow : public juce::Component {
  public:
    enum class View { Mix, Key, Velocity };
    using Zone = daw::audio::SamplerZoneSettings;

    SamplerZoneRow();
    ~SamplerZoneRow() override;

    void setZone(const Zone& zone, int index, bool selected);
    void setView(View view);

    std::function<void(int index)> onSelect;
    /// Once per gesture, when it ends.
    std::function<void(int index, const Zone& zone)> onEdit;
    std::function<void(int index)> onRemove;

    static constexpr int kHeight = 28;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void lookAndFeelChanged() override;

  private:
    void styleControls();
    void edit(const Zone& zone);

    Zone zone_;
    View view_ = View::Mix;
    int index_ = 0;
    bool selected_ = false;
    bool hovered_ = false;
    bool dragging_ = false;

    magda::DraggableValueLabel gainLabel_{magda::DraggableValueLabel::Format::Decibels};
    magda::SvgButton removeButton_{"Close", BinaryData::close_svg, BinaryData::close_svgSize};
    juce::TextButton roundRobinButton_{"RR"};
    ZoneBar zoneBar_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerZoneRow)
};

}  // namespace magda::daw::ui
