#pragma once

#include "../../common/ColourSwatch.hpp"
#include "../../common/DraggableValueLabel.hpp"
#include "../../common/SvgButton.hpp"
#include "BaseInspector.hpp"
#include "core/AutomationManager.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for automation clips (issue #1087).
 *
 * Edits the selected clip's fundamental properties:
 *   - Start / End / Length : timeline position in bars.beats
 *   - Loop                 : on/off + loop length in beats
 *
 * Start/End/Length edits go through the undoable clip commands; the loop
 * toggle and loop length are property setters (same convention as the lane
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

    // Header icons, matching the clip inspector: view (arrangement) + type
    // (automation clip). Non-interactive.
    std::unique_ptr<magda::SvgButton> viewIcon_;
    std::unique_ptr<magda::SvgButton> typeIcon_;
    std::unique_ptr<magda::ColourSwatch> colourSwatch_;
    juce::Label titleLabel_;
    juce::Label startLabel_, endLabel_, lengthLabel_;
    std::unique_ptr<magda::DraggableValueLabel> startValue_;
    std::unique_ptr<magda::DraggableValueLabel> endValue_;
    std::unique_ptr<magda::DraggableValueLabel> lengthValue_;

    juce::Label loopLabel_, loopLengthLabel_;
    std::unique_ptr<magda::SvgButton> loopToggle_;
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
