#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../interfaces/clip_interface.hpp"
#include "../interfaces/daw_mode_interface.hpp"
#include "../interfaces/track_interface.hpp"

/**
 * @brief Performance view - clip launcher view like Ableton Live Session View
 *
 * Shows:
 * - Grid of clips organized by track
 * - Clip launch buttons
 * - Scene launch buttons
 * - Real-time clip status
 */
class PerformanceView : public juce::Component {
  public:
    PerformanceView();
    ~PerformanceView() override = default;

    /**
     * @brief Set the DAW interfaces
     */
    void setInterfaces(DAWModeInterface* modeInterface, TrackInterface* tracks,
                       ClipInterface* clips);

    /**
     * @brief Update the view to reflect current DAW state
     */
    void updateView();

    // juce::Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // Main grid
    juce::Viewport gridViewport_;
    juce::Component clipGrid_;

    // Track headers
    juce::Component trackHeaders_;
    juce::Component sceneHeaders_;

    // Clip buttons (organized by track and scene)
    std::vector<std::vector<juce::TextButton*>> clipButtons_;

    // Scene launch buttons
    std::vector<juce::TextButton*> sceneButtons_;

    // DAW interfaces
    DAWModeInterface* modeInterface_ = nullptr;
    TrackInterface* trackInterface_ = nullptr;
    ClipInterface* clipInterface_ = nullptr;

    // Grid configuration
    static constexpr int MAX_TRACKS = 8;
    static constexpr int MAX_SCENES = 8;
    static constexpr int CLIP_BUTTON_SIZE = 80;

    // Event handlers
    void onClipClicked(int trackIndex, int sceneIndex);
    void onSceneClicked(int sceneIndex);

    // Helper methods
    void createClipGrid();
    void updateClipStates();
    juce::Colour getClipColor(const std::string& clipId, bool isPlaying) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformanceView)
};
