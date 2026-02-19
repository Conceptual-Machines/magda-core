#pragma once

#include <unordered_set>

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
 * - Per-clip mix (volume, pan, gain)
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
     * @brief Set the currently selected clips (batch)
     * @param clipIds Set of clip IDs to inspect
     */
    void setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds);

    /**
     * @brief Set the currently selected clip (convenience wrapper)
     * @param clipId The clip to inspect (INVALID_CLIP_ID for none)
     */
    void setSelectedClip(magda::ClipId clipId);

    // ClipManagerListener interface
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;

  private:
    // Initialization helpers (split from constructor for code size)
    void initClipPropertiesSection();
    void initSessionLaunchSection();
    void initPitchSection();
    void initMixSection();
    void initPlaybackSection();
    void initFadesSection();
    void initChannelsSection();
    void initViewport();

    // Current selection (supports single and multi-clip)
    std::unordered_set<magda::ClipId> selectedClipIds_;

    /** @brief Returns the primary clip ID (first in set) or INVALID_CLIP_ID if empty */
    magda::ClipId primaryClipId() const {
        return selectedClipIds_.empty() ? magda::INVALID_CLIP_ID : *selectedClipIds_.begin();
    }

    // Multi-selection count label
    juce::Label clipCountLabel_;

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
    juce::Label clipOffsetLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipContentOffsetValue_;

    // Loop section
    std::unique_ptr<magda::SvgButton> clipLoopToggle_;
    juce::Label clipLoopStartLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopStartValue_;
    juce::Label clipLoopEndLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopEndValue_;
    juce::Label clipLoopPhaseLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopPhaseValue_;

    // Warp/tempo section
    juce::TextButton clipWarpToggle_;
    juce::TextButton clipAutoTempoToggle_;
    std::unique_ptr<magda::DraggableValueLabel> clipStretchValue_;
    juce::ComboBox stretchModeCombo_;
    juce::Label clipBpmValue_;
    std::unique_ptr<magda::DraggableValueLabel> clipBeatsLengthValue_;

    // Pitch section (audio + MIDI)
    juce::Label pitchSectionLabel_;
    juce::TextButton midiTransposeUpBtn_;
    juce::TextButton midiTransposeDownBtn_;
    juce::Label midiTransposeLabel_;
    juce::TextButton autoPitchToggle_;
    juce::TextButton analogPitchToggle_;
    juce::ComboBox autoPitchModeCombo_;
    std::unique_ptr<magda::DraggableValueLabel> pitchChangeValue_;
    std::unique_ptr<magda::DraggableValueLabel> transposeValue_;

    // Beat detection section
    juce::Label beatDetectionSectionLabel_;
    juce::TextButton autoDetectBeatsToggle_;
    std::unique_ptr<magda::DraggableValueLabel> beatSensitivityValue_;

    // Transient detection section
    juce::Label transientSectionLabel_;
    juce::Label transientSensitivityLabel_;
    std::unique_ptr<magda::DraggableValueLabel> transientSensitivityValue_;

    // Playback
    juce::TextButton reverseToggle_;

    // Per-clip mix section
    juce::Label clipMixSectionLabel_;
    std::unique_ptr<magda::DraggableValueLabel> clipVolumeValue_;
    std::unique_ptr<magda::DraggableValueLabel> clipPanValue_;
    std::unique_ptr<magda::DraggableValueLabel> clipGainValue_;

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

    // Multi-selection range cache
    struct ClipRange {
        bool valid = false;  // True if at least one clip was processed
        float minPitchChange = 0.0f, maxPitchChange = 0.0f;
        int minTranspose = 0, maxTranspose = 0;
        float minVolumeDB = 0.0f, maxVolumeDB = 0.0f;
        float minPan = 0.0f, maxPan = 0.0f;
        float minGainDB = 0.0f, maxGainDB = 0.0f;
        double minFadeIn = 0.0, maxFadeIn = 0.0;
        double minFadeOut = 0.0, maxFadeOut = 0.0;
        double minSpeedRatio = 1.0, maxSpeedRatio = 1.0;
        double minStartSeconds = 0.0, maxStartSeconds = 0.0;
        double minLengthSeconds = 0.0, maxLengthSeconds = 0.0;
        double minOffsetSeconds = 0.0, maxOffsetSeconds = 0.0;
        // Type flags
        bool allAudio = true, allMidi = true;
        bool allArrangement = true, allSession = true;
    };
    ClipRange clipRange_;

    // Drag-start tracking for multi-selection delta edits
    double multiPitchChangeDragStart_ = 0.0;
    double multiTransposeDragStart_ = 0.0;
    double multiVolumeDragStart_ = 0.0;
    double multiPanDragStart_ = 0.0;
    double multiGainDragStart_ = 0.0;
    double multiFadeInDragStart_ = 0.0;
    double multiFadeOutDragStart_ = 0.0;
    double multiSpeedRatioDragStart_ = 0.0;
    double multiStartDragStart_ = 0.0;
    double multiEndDragStart_ = 0.0;
    double multiOffsetDragStart_ = 0.0;

    // Update methods
    void updateFromSelectedClip();
    void showClipControls(bool show);
    void computeClipRange();
    void refreshClipRangeDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipInspector)
};

}  // namespace magda::daw::ui
