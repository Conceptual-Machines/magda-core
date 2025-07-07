#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../interfaces/transport_interface.hpp"
#include "../interfaces/track_interface.hpp"
#include "../interfaces/clip_interface.hpp"

/**
 * @brief Arrangement view - traditional timeline view like most DAWs
 * 
 * Shows:
 * - Timeline with tracks
 * - Clips arranged on timeline
 * - Transport controls
 * - Track mixer
 */
class ArrangementView : public juce::Component {
public:
    ArrangementView();
    ~ArrangementView() override = default;
    
    /**
     * @brief Set the DAW interfaces
     */
    void setInterfaces(TransportInterface* transport,
                      TrackInterface* tracks,
                      ClipInterface* clips);
    
    /**
     * @brief Update the view to reflect current DAW state
     */
    void updateView();
    
    // juce::Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    
private:
    // Main components
    juce::Viewport timelineViewport_;
    juce::Viewport mixerViewport_;
    juce::Viewport transportViewport_;
    
    // Timeline components
    juce::Component timelineComponent_;
    juce::Component tracksComponent_;
    juce::Component clipsComponent_;
    
    // Mixer components
    juce::Component mixerComponent_;
    juce::Component masterFader_;
    
    // Transport components
    juce::Component transportComponent_;
    juce::TextButton playButton_;
    juce::TextButton stopButton_;
    juce::TextButton recordButton_;
    juce::Slider tempoSlider_;
    juce::Label tempoLabel_;
    
    // DAW interfaces
    TransportInterface* transportInterface_ = nullptr;
    TrackInterface* trackInterface_ = nullptr;
    ClipInterface* clipInterface_ = nullptr;
    
    // Event handlers
    void onPlayClicked();
    void onStopClicked();
    void onRecordClicked();
    void onTempoChanged();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArrangementView)
}; 