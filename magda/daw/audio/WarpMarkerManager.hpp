#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdint>
#include <map>

#include "../core/ClipManager.hpp"
#include "../core/ClipTypes.hpp"

namespace magda {

/**
 * @brief Native transient detection, off the message thread, cached per source file.
 *
 * Detection follows the clip's beatSensitivity; any change to it, including undo, re-detects.
 */
class WarpMarkerManager : private juce::Timer, private ClipManagerListener {
  public:
    static WarpMarkerManager& getInstance();
    ~WarpMarkerManager() override;
    bool getTransientTimes(ClipId clipId);
    /// Debounced; the settled value is written through the undo stack.
    void setTransientSensitivity(ClipId clipId, float sensitivity);
    bool hasPendingSensitivity() const {
        return !pending_.empty();
    }
    void stopBackgroundWork();

  private:
    WarpMarkerManager();
    void startDetection(ClipId clipId);
    void timerCallback() override;
    void clipsChanged() override {}
    void clipPropertyChanged(ClipId clipId) override;

    juce::ThreadPool workers_{1};
    std::map<ClipId, float> pending_;
    std::map<juce::String, std::uint64_t> active_;
    // Sensitivity each path's in-flight or cached result was detected with.
    std::map<juce::String, float> detectedWith_;
    std::uint64_t generation_ = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE(WarpMarkerManager)
};

}  // namespace magda
