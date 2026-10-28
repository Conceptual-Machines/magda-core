// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../../common/ColourSwatch.hpp"
#include "../../common/DraggableValueLabel.hpp"
#include "../../common/ToggleSwitch.hpp"
#include "BaseInspector.hpp"
#include "core/AutomationManager.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for automation clips (issue #1087).
 *
 * Laid out like the clip inspector: a colour spine and name over a summary line,
 * then a Clip row (start, end, length) and a Loop row (switch, loop length).
 *
 * Start/End/Length edits go through the undoable clip commands; the loop
 * switch and loop length are property setters (same convention as the lane
 * header toggles). Selection is pushed in by InspectorContainer; the
 * inspector listens to AutomationManager for live refreshes.
 */
class AutomationClipInspector : public BaseInspector, private magda::AutomationManagerListener {
  public:
    AutomationClipInspector();
    ~AutomationClipInspector() override;

    void onActivated() override;
    void onDeactivated() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setSelectedClip(const magda::AutomationClipSelection& selection);

  private:
    magda::AutomationClipSelection selection_;

    std::unique_ptr<magda::ColourSwatch> colourSwatch_;
    juce::Label titleLabel_;
    juce::Label subline_;
    juce::Label startLabel_, endLabel_, lengthLabel_;  // column headings
    juce::Label clipRowLabel_;
    std::unique_ptr<magda::DraggableValueLabel> startValue_;
    std::unique_ptr<magda::DraggableValueLabel> endValue_;
    std::unique_ptr<magda::DraggableValueLabel> lengthValue_;

    juce::Label loopLabel_;
    ToggleSwitch loopSwitch_;
    std::unique_ptr<magda::DraggableValueLabel> loopLengthValue_;

    const magda::AutomationClipInfo* getClip() const;
    void updateFromSelection();
    void showControls(bool show);
    void refreshDisplay();

    // AutomationManagerListener
    void automationLanesChanged() override {}
    void automationClipsChanged(magda::AutomationLaneId laneId) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationClipInspector)
};

}  // namespace magda::daw::ui
