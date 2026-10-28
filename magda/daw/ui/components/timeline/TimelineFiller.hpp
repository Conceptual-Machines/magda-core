// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

class TimelineFiller : public juce::Component {
  public:
    TimelineFiller();
    ~TimelineFiller() override = default;

    void paint(juce::Graphics& g) override;

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineFiller)
};

}  // namespace magda
