#pragma once

#include "../common/DraggableValueLabel.hpp"
#include "../mixer/RoutingSelector.hpp"
#include "PanelContent.hpp"
#include "core/ClipManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"

namespace magica {
class TimelineController;  // Forward declaration
}

namespace magica::daw::ui {

/**
 * @brief Inspector panel content
 *
 * Displays properties of the currently selected track or clip.
 * Dynamically switches between track and clip properties based on selection.
 */
class InspectorContent : public PanelContent,
                         public magica::TrackManagerListener,
                         public magica::ClipManagerListener,
                         public magica::SelectionManagerListener {
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
    void setTimelineController(magica::TimelineController* controller);

    // TrackManagerListener
    void tracksChanged() override;
    void trackPropertyChanged(int trackId) override;
    void trackSelectionChanged(magica::TrackId trackId) override;

    // ClipManagerListener
    void clipsChanged() override;
    void clipPropertyChanged(magica::ClipId clipId) override;
    void clipSelectionChanged(magica::ClipId clipId) override;

    // SelectionManagerListener
    void selectionTypeChanged(magica::SelectionType newType) override;

  private:
    juce::Label titleLabel_;
    juce::Label noSelectionLabel_;

    // Timeline controller reference (for tempo/time signature)
    magica::TimelineController* timelineController_ = nullptr;

    // Current selection state
    magica::SelectionType currentSelectionType_ = magica::SelectionType::None;
    magica::TrackId selectedTrackId_ = magica::INVALID_TRACK_ID;
    magica::ClipId selectedClipId_ = magica::INVALID_CLIP_ID;

    // Track properties section
    juce::Label trackNameLabel_;
    juce::Label trackNameValue_;
    juce::TextButton muteButton_;
    juce::TextButton soloButton_;
    juce::TextButton recordButton_;
    std::unique_ptr<magica::DraggableValueLabel> gainLabel_;
    std::unique_ptr<magica::DraggableValueLabel> panLabel_;

    // Routing section (MIDI/Audio In/Out)
    juce::Label routingSectionLabel_;
    std::unique_ptr<magica::RoutingSelector> audioInSelector_;
    std::unique_ptr<magica::RoutingSelector> audioOutSelector_;
    std::unique_ptr<magica::RoutingSelector> midiInSelector_;
    std::unique_ptr<magica::RoutingSelector> midiOutSelector_;

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
    juce::Label clipStartLabel_;
    juce::Label clipStartValue_;
    juce::Label clipLengthLabel_;
    juce::Label clipLengthValue_;
    juce::ToggleButton clipLoopToggle_;
    juce::Label clipLoopLengthLabel_;
    juce::Slider clipLoopLengthSlider_;
    juce::Label clipTypeLabel_;
    juce::Label clipTypeValue_;

    void updateFromSelectedTrack();
    void updateFromSelectedClip();
    void showTrackControls(bool show);
    void showClipControls(bool show);
    void updateSelectionDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InspectorContent)
};

}  // namespace magica::daw::ui
