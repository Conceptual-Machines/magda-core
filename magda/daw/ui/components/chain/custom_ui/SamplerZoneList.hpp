// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "custom_ui/SamplerZoneRow.hpp"
#include "layout/DashedAddButton.hpp"
#include "plugins/sampler/SamplerZones.hpp"

namespace magda::daw::ui {

/**
 * @brief A sampler's zones as a rack lists its chains (#3098, #3100): MIX / KEY / VEL tabs over a
 *        row per zone, then Add zone.
 */
class SamplerZoneList : public juce::Component {
  public:
    using Zone = daw::audio::SamplerZoneSettings;

    SamplerZoneList();

    void setZones(const std::vector<Zone>& zones, int selected);
    /// The row under @p position, in this component's coordinates, or -1.
    int rowAt(juce::Point<int> position) const;

    std::function<void(int index)> onSelect;
    std::function<void(int index, const Zone& zone)> onEdit;
    std::function<void(int index)> onRemove;
    /// Add zone was pressed.
    std::function<void()> onAdd;

    static constexpr int kTabsHeight = 24;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    juce::Rectangle<int> tabBounds(SamplerZoneRow::View view) const;

    SamplerZoneRow::View view_ = SamplerZoneRow::View::Mix;
    juce::Viewport viewport_;
    juce::Component content_;
    std::vector<std::unique_ptr<SamplerZoneRow>> rows_;
    DashedAddButton addButton_{"Add zone"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerZoneList)
};

}  // namespace magda::daw::ui
