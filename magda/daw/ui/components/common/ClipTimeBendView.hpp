// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "core/ClipManager.hpp"
#include "ui/components/common/ClipTimeBendEdit.hpp"
#include "ui/components/common/TimeBendEditor.hpp"

namespace magda::daw::ui {

/**
 * @brief MIDI clips' time bend, edited live (#3044): the editor, a groove menu and Bake.
 *
 * Shows the first clip's bend; an edit applies to every clip as one undo step. Follows the clips
 * through ClipManager, so an undo or another view's edit lands here too.
 */
class ClipTimeBendView : public juce::Component, private magda::ClipManagerListener {
  public:
    ClipTimeBendView();
    ~ClipTimeBendView() override;

    void setClips(std::vector<magda::ClipId> clipIds);
    const std::vector<magda::ClipId>& clips() const {
        return clipIds_;
    }

    void resized() override;

  private:
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void reload();
    void showGrooveMenu();
    void loadGroove(const juce::String& name);

    std::vector<magda::ClipId> clipIds_;
    TimeBendEditor editor_;
    ClipTimeBendEdit edit_;
    juce::TextButton grooveButton_{"GROOVE"};
    juce::TextButton bakeButton_{"BAKE"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipTimeBendView)
};

}  // namespace magda::daw::ui
