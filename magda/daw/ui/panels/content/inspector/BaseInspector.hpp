#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {
class TimelineController;
class AudioEngine;
}  // namespace magda

namespace magda::daw::ui {

/**
 * @brief Abstract base class for all inspector types
 *
 * Provides common interface for displaying and editing properties
 * of different entity types (Tracks, Clips, Notes, Devices, etc.).
 *
 * Each concrete inspector is responsible for:
 * - Creating and managing its UI components
 * - Listening to relevant model changes
 * - Updating UI from model state
 * - Applying user edits back to model
 */
class BaseInspector : public juce::Component {
  public:
    BaseInspector() = default;
    ~BaseInspector() override = default;

    /**
     * @brief Called when the inspector is activated (shown)
     * Use this to register listeners and update initial state
     */
    virtual void onActivated() = 0;

    /**
     * @brief Called when the inspector is deactivated (hidden)
     * Use this to unregister listeners and clean up
     */
    virtual void onDeactivated() = 0;

    /**
     * @brief Set the timeline controller reference
     * @param controller Timeline controller for accessing tempo/time signature
     */
    virtual void setTimelineController(magda::TimelineController* controller) {
        timelineController_ = controller;
    }

    /**
     * @brief Set the audio engine reference
     * @param engine Audio engine for accessing audio/MIDI devices
     */
    virtual void setAudioEngine(magda::AudioEngine* engine) {
        audioEngine_ = engine;
    }

  protected:
    // Shared dependencies available to all inspectors
    magda::TimelineController* timelineController_ = nullptr;
    magda::AudioEngine* audioEngine_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BaseInspector)
};

}  // namespace magda::daw::ui
