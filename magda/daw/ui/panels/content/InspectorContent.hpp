#pragma once

#include "../../state/TimelineController.hpp"
#include "../common/BarsBeatsTicksLabel.hpp"
#include "../common/DraggableValueLabel.hpp"
#include "../common/SvgButton.hpp"
#include "../mixer/RoutingSelector.hpp"
#include "PanelContent.hpp"
#include "core/ClipManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"

namespace magda {
class AudioEngine;  // Forward declaration
}  // namespace magda

namespace magda::daw::ui {

/**
 * @brief Inspector panel content
 *
 * Displays properties of the currently selected track or clip.
 * Dynamically switches between track and clip properties based on selection.
 */
class InspectorContent : public PanelContent,
                         public magda::TrackManagerListener,
                         public magda::ClipManagerListener,
                         public magda::SelectionManagerListener,
                         public magda::TimelineStateListener {
  public:
    InspectorContent();
    ~InspectorContent() override;

    PanelContentType getContentType() const override {
        return PanelContentType::Inspector;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::Inspector, "Inspector", "Selection properties", "Inspector"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

    // Set the timeline controller reference (for accessing tempo/time signature)
    void setTimelineController(magda::TimelineController* controller);

    // Set the audio engine reference (for accessing audio/MIDI devices)
    void setAudioEngine(magda::AudioEngine* engine);

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackSelectionChanged(magda::TrackId trackId) override;
    void deviceParameterChanged(magda::DeviceId deviceId, int paramIndex, float newValue) override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(magda::ClipId clipId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;

    // SelectionManagerListener
    void selectionTypeChanged(magda::SelectionType newType) override;
    void noteSelectionChanged(const magda::NoteSelection& selection) override;
    void chainNodeSelectionChanged(const magda::ChainNodePath& path) override;
    void modSelectionChanged(const magda::ModSelection& selection) override;
    void macroSelectionChanged(const magda::MacroSelection& selection) override;
    void modsPanelSelectionChanged(const magda::ModsPanelSelection& selection) override;
    void macrosPanelSelectionChanged(const magda::MacrosPanelSelection& selection) override;
    void paramSelectionChanged(const magda::ParamSelection& selection) override;

    // TimelineStateListener
    void timelineStateChanged(const magda::TimelineState& state,
                              magda::ChangeFlags changes) override;

  private:
    juce::Label titleLabel_;
    juce::Label noSelectionLabel_;

    // Timeline controller reference (for tempo/time signature)
    magda::TimelineController* timelineController_ = nullptr;

    // Audio engine reference (for audio/MIDI devices)
    magda::AudioEngine* audioEngine_ = nullptr;

    // Current selection state
    magda::SelectionType currentSelectionType_ = magda::SelectionType::None;
    magda::TrackId selectedTrackId_ = magda::INVALID_TRACK_ID;
    magda::ClipId selectedClipId_ = magda::INVALID_CLIP_ID;
    magda::NoteSelection noteSelection_;
    magda::ChainNodePath selectedChainNode_;

    // Track properties section
    juce::Label trackNameLabel_;
    juce::Label trackNameValue_;
    juce::TextButton muteButton_;
    juce::TextButton soloButton_;
    juce::TextButton recordButton_;
    std::unique_ptr<magda::DraggableValueLabel> gainLabel_;
    std::unique_ptr<magda::DraggableValueLabel> panLabel_;

    // Routing section (MIDI/Audio In/Out)
    juce::Label routingSectionLabel_;
    std::unique_ptr<magda::RoutingSelector> audioInSelector_;
    std::unique_ptr<magda::RoutingSelector> audioOutSelector_;
    std::unique_ptr<magda::RoutingSelector> midiInSelector_;
    std::unique_ptr<magda::RoutingSelector> midiOutSelector_;

    // Send/Receive section
    juce::Label sendReceiveSectionLabel_;
    juce::Label sendsLabel_;
    juce::Label receivesLabel_;

    // Clips section
    juce::Label clipsSectionLabel_;
    juce::Label clipCountLabel_;

    // Clip properties section
    juce::Label clipNameLabel_;
    juce::Label clipNameValue_;
    juce::Label clipFilePathLabel_;                   // Full file path (read-only, below clip name)
    std::unique_ptr<magda::SvgButton> clipTypeIcon_;  // Audio (sinewave) or MIDI icon
    juce::Label playbackColumnLabel_;
    juce::Label loopColumnLabel_;
    std::unique_ptr<magda::SvgButton> clipPositionIcon_;
    juce::Label clipStartLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipStartValue_;
    juce::Label clipEndLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipEndValue_;
    juce::Label clipOffsetRowLabel_;
    std::unique_ptr<magda::SvgButton> clipContentOffsetIcon_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipContentOffsetValue_;
    std::unique_ptr<magda::SvgButton> clipLoopToggle_;
    std::unique_ptr<magda::SvgButton> clipWarpToggle_;
    std::unique_ptr<magda::SvgButton> clipAutoTempoToggle_;  // Musical mode toggle
    std::unique_ptr<magda::DraggableValueLabel> clipStretchValue_;
    juce::ComboBox stretchModeCombo_;  // Time stretch algorithm selector
    juce::Label clipLoopStartLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopStartValue_;
    juce::Label clipLoopLengthLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopLengthValue_;
    juce::Label clipLoopPhaseLabel_;
    std::unique_ptr<magda::BarsBeatsTicksLabel> clipLoopPhaseValue_;
    juce::Label clipBpmValue_;  // Detected BPM (no label)
    std::unique_ptr<magda::DraggableValueLabel>
        clipBeatsLengthValue_;  // Length in beats for auto-tempo

    // Clip properties viewport (scrollable container for all clip controls)
    juce::Viewport clipPropsViewport_;
    juce::Component clipPropsContainer_;

    // Pitch section
    juce::Label pitchSectionLabel_;
    juce::TextButton autoPitchToggle_;
    juce::ComboBox autoPitchModeCombo_;
    std::unique_ptr<magda::DraggableValueLabel> pitchChangeValue_;
    std::unique_ptr<magda::DraggableValueLabel> transposeValue_;

    // Beat Detection section
    juce::Label beatDetectionSectionLabel_;
    juce::TextButton autoDetectBeatsToggle_;
    std::unique_ptr<magda::DraggableValueLabel> beatSensitivityValue_;

    // Playback
    juce::TextButton reverseToggle_;

    // Per-Clip Mix section
    juce::Label clipMixSectionLabel_;
    std::unique_ptr<magda::DraggableValueLabel> clipGainValue_;
    std::unique_ptr<magda::DraggableValueLabel> clipPanValue_;

    // Fades section
    juce::Label fadesSectionLabel_;
    std::unique_ptr<magda::DraggableValueLabel> fadeInValue_;
    std::unique_ptr<magda::DraggableValueLabel> fadeOutValue_;
    juce::ComboBox fadeInTypeCombo_;
    juce::ComboBox fadeOutTypeCombo_;
    juce::ComboBox fadeInBehaviourCombo_;
    juce::ComboBox fadeOutBehaviourCombo_;
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

    // Note properties section
    juce::Label noteCountLabel_;
    juce::Label notePitchLabel_;
    std::unique_ptr<magda::DraggableValueLabel> notePitchValue_;
    juce::Label noteVelocityLabel_;
    std::unique_ptr<magda::DraggableValueLabel> noteVelocityValue_;
    juce::Label noteStartLabel_;
    juce::Label noteStartValue_;
    juce::Label noteLengthLabel_;
    std::unique_ptr<magda::DraggableValueLabel> noteLengthValue_;

    // Chain node properties section
    juce::Label chainNodeTypeLabel_;
    juce::Label chainNodeNameLabel_;
    juce::Label chainNodeNameValue_;

    // Mods panel properties section
    juce::Label modsPanelTitleLabel_;
    juce::Label modsPanelPathLabel_;

    // Macros panel properties section
    juce::Label macrosPanelTitleLabel_;
    juce::Label macrosPanelPathLabel_;

    // Device parameters section
    struct DeviceParamControl {
        juce::Label nameLabel;
        juce::Label valueLabel;
        juce::Slider slider;
        int paramIndex;
    };

    juce::Label deviceParamsLabel_;
    juce::Viewport deviceParamsViewport_;
    juce::Component deviceParamsContainer_;
    std::vector<std::unique_ptr<DeviceParamControl>> deviceParamControls_;

    void updateFromSelectedTrack();
    void updateFromSelectedClip();
    void updateFromSelectedNotes();
    void updateFromSelectedChainNode();
    void updateFromSelectedModsPanel();
    void updateFromSelectedMacrosPanel();
    void showTrackControls(bool show);
    void showClipControls(bool show);
    void showNoteControls(bool show);
    void showChainNodeControls(bool show);
    void showModsPanelControls(bool show);
    void showMacrosPanelControls(bool show);
    void updateSelectionDisplay();
    void populateRoutingSelectors();
    void populateAudioInputOptions();
    void populateAudioOutputOptions();
    void populateMidiInputOptions();
    void populateMidiOutputOptions();
    void updateRoutingSelectorsFromTrack();

    void createDeviceParamControls(const magda::DeviceInfo& device);
    void showDeviceParamControls(bool show);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InspectorContent)
};

}  // namespace magda::daw::ui
