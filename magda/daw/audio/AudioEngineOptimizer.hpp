#pragma once

#include <juce_core/juce_core.h>

#include "core/ViewModeController.hpp"
#include "core/ViewModeState.hpp"

namespace magda {

/**
 * @brief Bridge to apply audio profiles to the audio engine
 *
 * This class listens for view mode changes and applies the corresponding
 * audio engine optimization profile. Currently a stub for future
 * TracktionEngineWrapper integration.
 */
class AudioEngineOptimizer final : public ViewModeListener {
  public:
    AudioEngineOptimizer();
    ~AudioEngineOptimizer() override;

    // ViewModeListener interface
    void viewModeChanged(ViewMode mode, const AudioEngineProfile& profile) override;

    /**
     * Apply an audio profile to the engine
     */
    void applyProfile(const AudioEngineProfile& profile);

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngineOptimizer)
};

}  // namespace magda
