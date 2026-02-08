#pragma once

#include <memory>

#include "BaseInspector.hpp"
#include "core/SelectionManager.hpp"

namespace magda {
class TimelineController;
class AudioEngine;
}  // namespace magda

namespace magda::daw::ui {

/**
 * @brief Container that manages specialized inspectors based on selection
 *
 * Replaces InspectorContent by delegating to specialized inspectors:
 * - Listens to SelectionManager for selection changes
 * - Uses InspectorFactory to create appropriate inspector
 * - Manages inspector lifetime and layout
 * - Shows "No selection" message when nothing is selected
 *
 * This architecture keeps each inspector focused (~200-1000 LOC) and
 * makes it easy to add new inspector types in the future.
 */
class InspectorContainer : public juce::Component, public magda::SelectionManagerListener {
  public:
    InspectorContainer();
    ~InspectorContainer() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Set the timeline controller reference
     * @param controller Timeline controller for accessing tempo/time signature
     */
    void setTimelineController(magda::TimelineController* controller);

    /**
     * @brief Set the audio engine reference
     * @param engine Audio engine for accessing audio/MIDI devices
     */
    void setAudioEngine(magda::AudioEngine* engine);

    // SelectionManagerListener interface
    void selectionTypeChanged(magda::SelectionType newType) override;
    void trackSelectionChanged(magda::TrackId trackId) override;
    void clipSelectionChanged(magda::ClipId clipId) override;
    void noteSelectionChanged(const magda::NoteSelection& selection) override;
    void chainNodeSelectionChanged(const magda::ChainNodePath& path) override;

  private:
    // Current inspector (nullptr when no selection)
    std::unique_ptr<BaseInspector> currentInspector_;
    magda::SelectionType currentSelectionType_ = magda::SelectionType::None;

    // No selection message
    juce::Label noSelectionLabel_;

    // Shared dependencies passed to inspectors
    magda::TimelineController* timelineController_ = nullptr;
    magda::AudioEngine* audioEngine_ = nullptr;

    // Update methods
    void switchToInspector(magda::SelectionType type);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InspectorContainer)
};

}  // namespace magda::daw::ui
