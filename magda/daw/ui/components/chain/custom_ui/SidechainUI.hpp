#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"
#include "ui/components/chain/modulation/LFOCurveEditor.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Faceplate for the Sidechain volume-shaper insert (issue #1591).
 *
 * The main area is the LFO curve editor bound to the device's bundled curve
 * modulator (DeviceInfo::mods[0], seeded at creation). Below it one control
 * row: duck depth (the mod link's amount on the gain param), trigger sync
 * division, loop/one-shot mode, and attack/release gain smoothing (plugin
 * params, macro/mod linkable). The MIDI source track is picked with the
 * conventional sidechain button on the device slot header, not here.
 *
 * The curve editor and the depth/division/mode controls write through the
 * existing mod APIs on TrackManager, addressed by the slot's node path + mod
 * index 0; attack/release go through the standard onParameterChanged path.
 */
class SidechainUI : public juce::Component {
  public:
    SidechainUI();
    ~SidechainUI() override = default;

    // Queried at interaction time, not capture time — the slot's path can
    // change after create() (section-scoped ids, drag reorder).
    std::function<magda::ChainNodePath()> getNodePath;
    std::function<void(int paramIndex, float value)> onParameterChanged;

    void updateFromDevice(const magda::DeviceInfo& device);
    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    // Re-resolve the live model through getNodePath and rebind the curve
    // editor + depth/division/source controls. Needed after the slot's path
    // becomes valid: create() runs before the slot knows its path, so the
    // first updateFromDevice cannot bind the curve editor yet.
    void refreshFromModel();

    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    magda::ChainNodePath currentPath() const;
    // Live model pointer (the update() snapshot's mods would dangle).
    magda::DeviceInfo* liveDevice() const;
    void bindCurveEditor();

    magda::LFOCurveEditor curveEditor_;

    juce::Label depthLabel_, divisionLabel_, modeLabel_, attackLabel_, releaseLabel_;
    juce::TextButton modeButton_;  // Loop vs 1-Shot (ModInfo::oneShot)
    TextSlider depthSlider_;
    TextSlider divisionSlider_;
    LinkableTextSlider attackSlider_, releaseSlider_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidechainUI)
};

}  // namespace magda::daw::ui
