// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace magda {

class TimelineComponent;

class TimelineHeaderPanel : public juce::Component {
  public:
    TimelineHeaderPanel();
    ~TimelineHeaderPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Layout alignment with main window panels
    void setLayoutSizes(int leftWidth, int rightWidth);

    // Timeline component access
    TimelineComponent* getTimelineComponent() const {
        return timeline.get();
    }

  private:
    std::unique_ptr<TimelineComponent> timeline;

    // Layout sizes to align with main window panels
    int leftPanelWidth = 250;
    int rightPanelWidth = 300;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineHeaderPanel)
};

}  // namespace magda
