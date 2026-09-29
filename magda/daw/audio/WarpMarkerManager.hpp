#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdint>
#include <map>

#include "../core/ClipTypes.hpp"

namespace magda {

/** Native transient detection, off the message thread, cached per source file. */
class WarpMarkerManager : private juce::Timer {
  public:
    static WarpMarkerManager& getInstance();
    ~WarpMarkerManager() override;
    bool getTransientTimes(ClipId clipId);
    void setTransientSensitivity(ClipId clipId, float sensitivity);
    void stopBackgroundWork();

  private:
    void startDetection(ClipId clipId);
    void timerCallback() override;

    juce::ThreadPool workers_{1};
    std::map<ClipId, float> pending_;
    std::map<juce::String, std::uint64_t> active_;
    std::uint64_t generation_ = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE(WarpMarkerManager)
};

}  // namespace magda
