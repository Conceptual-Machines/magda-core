#pragma once

#include "../../common/BarsBeatsTicksLabel.hpp"
#include "../../common/DraggableValueLabel.hpp"
#include "../../common/SvgButton.hpp"
#include "BaseInspector.hpp"
#include "core/ClipManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for clip properties
 *
 * Displays and edits comprehensive clip properties:
 * - Position (start, end, length, offset)
 * - Loop controls (toggle, start, length, phase)
 * - Warp/auto-tempo/stretch settings
 * - Pitch (auto-pitch, transpose)
 * - Per-clip mix (gain, pan)
 * - Fades (in/out with type/behavior controls)
 * - Playback (reverse, channels)
 * - Session launch settings (mode, quantize)
 */
class ClipInspector : public BaseInspector, public magda::ClipManagerListener {
  public:
    ClipInspector();
    ~ClipInspector() override;

    void onActivated() override;
    void onDeactivated() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Set the currently selected clip
     * @param clipId The clip to inspect (INVALID_CLIP_ID for none)
     */
    void setSelectedClip(magda::ClipId clipId);

    // ClipManagerListener interface
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;

  private:
    // Current selection
    magda::ClipId selectedClipId_ = magda::INVALID_CLIP_ID;

    // Clip name and file info
    juce::Label clipNameLabel_;
    juce::Label clipNameValue_;
    juce::Label clipFilePathLabel_;
    std::unique_ptr<magda::SvgButton> clipTypeIcon_;

    // Position section
    juce::Label playbackColumnLabel_;
    juce::Label loopColumnLabel_;
    std::unique_ptr<magda::SvgButton> clipPositionIcon_;
    juce::Label clipStartLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipStartValue_;
    juce::Label clipEndLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipEndValue_;
    juce::Label clipLengthLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLengthValue_;

    // Content offset section
    juce::Label clipOffsetRowLabel_;
    std::unique_ptr<magda::SvgButton> clipContentOffsetIcon_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipContentOffsetValue_;

    // Loop section
    std::unique_ptr<magda::SvgButton> clipLoopToggle_;
    juce::Label clipLoopStartLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopStartValue_;
    juce::Label clipLoopLengthLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopLengthValue_;
    juce::Label clipLoopPhaseLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopPhaseValue_;

    // Warp/tempo section
    juce::TextButton clipWarpToggle_;
    juce::TextButton clipAutoTempoToggle_;
    std::unique_ptr<magda::DraggableValueLabel> clipStretchValue_;
    juce::ComboBox stretchModeCombo_;
    juce::Label clipBpmValue_;
    std::unique_ptr<magda::DraggableValueLabel> clipBeatsLengthValue_;

    // Pitch section
    juce::Label pitchSectionLabel_;
    juce::TextButton autoPitchToggle_;
    juce::ComboBox autoPitchModeCombo_;
    std::unique_ptr<magda::DraggableValueLabel> pitchChangeValue_;
    std::unique_ptr<magda::DraggableValueLabel> transposeValue_;

    // Beat detection section
    juce::Label beatDetectionSectionLabel_;
    juce::TextButton autoDetectBeatsToggle_;
    std::unique_ptr<magda::DraggableValueLabel> beatSensitivityValue_;

    // Playback
    juce::TextButton reverseToggle_;

    // Per-clip mix section
    juce::Label clipMixSectionLabel_;
    std::unique_ptr<magda::DraggableValueLabel> clipGainValue_;
    std::unique_ptr<magda::DraggableValueLabel> clipPanValue_;

    // Fades section (collapsible)
    bool fadesCollapsed_ = false;
    juce::TextButton fadesCollapseToggle_;
    juce::Label fadesSectionLabel_;
    std::unique_ptr<magda::DraggableValueLabel> fadeInValue_;
    std::unique_ptr<magda::DraggableValueLabel> fadeOutValue_;
    std::unique_ptr<magda::SvgButton> fadeInTypeButtons_[4];
    std::unique_ptr<magda::SvgButton> fadeOutTypeButtons_[4];
    std::unique_ptr<magda::SvgButton> fadeInBehaviourButtons_[2];
    std::unique_ptr<magda::SvgButton> fadeOutBehaviourButtons_[2];
    juce::TextButton autoCrossfadeToggle_;

    // Channels section
    juce::Label channelsSectionLabel_;
    juce::TextButton leftChannelToggle_;
    juce::TextButton rightChannelToggle_;

    // Session clip launch properties
    juce::Label launchModeLabel_;
    juce::ComboBox launchModeCombo_;
    juce::Label launchQuantizeLabel_;
    juce::ComboBox launchQuantizeCombo_;

    // Scrollable container for clip properties
    juce::Viewport clipPropsViewport_;
    class ClipPropsContainer : public juce::Component {
      public:
        void paint(juce::Graphics& g) override;
        std::vector<int> separatorYPositions;
    };
    ClipPropsContainer clipPropsContainer_;

    // Update methods
    void updateFromSelectedClip();
    void showClipControls(bool show);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipInspector)
};

}  // namespace magda::daw::ui
