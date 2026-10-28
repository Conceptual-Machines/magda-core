// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <unordered_set>

#include "core/ClipManager.hpp"
#include "ui/components/common/ClipTimeBendView.hpp"
#include "ui/components/common/CollapsibleSectionHeader.hpp"

namespace magda::daw::ui {

/**
 * @brief The selected MIDI clips' time bend in the inspector (#3044), under its own header.
 *
 * The curve grows with the inspector's width. Hidden when no MIDI clip is selected.
 */
class ClipTimeBendSection : public juce::Component {
  public:
    ClipTimeBendSection();

    void setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds);

    /** @brief Height at @p width, 0 when there is nothing to show. */
    int getPreferredHeight(int width) const;

    void resized() override;

    std::function<void()> onCollapsedChanged;

  private:
    CollapsibleSectionHeader header_{"Time Bend", "clipInspector.timeBend"};
    ClipTimeBendView view_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipTimeBendSection)
};

}  // namespace magda::daw::ui
