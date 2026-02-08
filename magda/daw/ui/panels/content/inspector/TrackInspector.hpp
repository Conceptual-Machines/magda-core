#pragma once

#include <map>

#include "../../common/DraggableValueLabel.hpp"
#include "../../mixer/InputTypeSelector.hpp"
#include "../../mixer/RoutingSelector.hpp"
#include "BaseInspector.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inspector for track properties
 *
 * Displays and edits:
 * - Track name
 * - Mute/Solo/Record state
 * - Volume and Pan
 * - Audio/MIDI routing (input/output)
 * - Sends/Receives
 * - Clip count
 */
class TrackInspector : public BaseInspector, public magda::TrackManagerListener {
  public:
    TrackInspector();
    ~TrackInspector() override;

    void onActivated() override;
    void onDeactivated() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Set the currently selected track
     * @param trackId The track to inspect (INVALID_TRACK_ID for none)
     */
    void setSelectedTrack(magda::TrackId trackId);

    // TrackManagerListener interface
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackDevicesChanged(magda::TrackId trackId) override;
    void trackSelectionChanged(magda::TrackId trackId) override;
    void deviceParameterChanged(magda::DeviceId deviceId, int paramIndex, float newValue) override;

  private:
    // Current selection
    magda::TrackId selectedTrackId_ = magda::INVALID_TRACK_ID;

    // Track properties section
    juce::Label trackNameLabel_;
    juce::Label trackNameValue_;
    juce::TextButton muteButton_;
    juce::TextButton soloButton_;
    juce::TextButton recordButton_;
    std::unique_ptr<magda::DraggableValueLabel> gainLabel_;
    std::unique_ptr<magda::DraggableValueLabel> panLabel_;

    // Routing section (unified input type toggle + selectors)
    juce::Label routingSectionLabel_;
    std::unique_ptr<magda::InputTypeSelector> inputTypeSelector_;
    std::unique_ptr<magda::RoutingSelector> inputSelector_;       // Audio OR MIDI input
    std::unique_ptr<magda::RoutingSelector> outputSelector_;      // Audio output (always master)
    std::unique_ptr<magda::RoutingSelector> midiOutputSelector_;  // MIDI output device

    // Send/Receive section
    juce::Label sendReceiveSectionLabel_;
    juce::TextButton addSendButton_;
    std::vector<std::unique_ptr<juce::Label>> sendDestLabels_;
    std::vector<std::unique_ptr<magda::DraggableValueLabel>> sendLevelLabels_;
    std::vector<std::unique_ptr<juce::TextButton>> sendDeleteButtons_;
    juce::Label noSendsLabel_;
    juce::Label receivesLabel_;

    // Clips section
    juce::Label clipsSectionLabel_;
    juce::Label clipCountLabel_;

    // Update methods
    void updateFromSelectedTrack();
    void showTrackControls(bool show);
    void rebuildSendsUI();
    void showAddSendMenu();
    void populateRoutingSelectors();
    void populateAudioInputOptions();
    void populateAudioOutputOptions();
    void populateMidiInputOptions();
    void populateMidiOutputOptions();
    void updateRoutingSelectorsFromTrack();

    // Output routing: option ID → TrackId mapping for group/aux destinations
    std::map<int, magda::TrackId> outputTrackMapping_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackInspector)
};

}  // namespace magda::daw::ui
