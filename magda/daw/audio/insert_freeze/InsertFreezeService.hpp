#pragma once

#include <juce_events/juce_events.h>

#include <memory>

#include "core/TypeIds.hpp"

namespace tracktion {
inline namespace engine {
class Edit;
class InsertPlugin;
}  // namespace engine
}  // namespace tracktion

namespace magda {

class AudioBridge;
class InsertCapturePlugin;

/**
 * @brief Freeze-to-audio for External FX / External Instrument devices (#1623).
 *
 * Offline export cannot capture outboard gear, so freeze runs a real-time pass:
 * play the track through the live engine, record the insert's audio return
 * (post-InsertNode, PDC-aligned) into a wav via a hidden InsertCapturePlugin,
 * then replace the track's clips with the captured audio clip and bypass the
 * insert plus everything before it in the chain. Offline export then bounces
 * the clip through the still-live post-insert devices as normal.
 *
 * One pass at a time. All methods are message-thread only.
 */
class InsertFreezeService : private juce::Timer {
  public:
    InsertFreezeService(tracktion::engine::Edit& edit, AudioBridge& audioBridge);
    ~InsertFreezeService() override;

    bool isFreezing() const {
        return pass_ != nullptr;
    }
    bool isFreezing(TrackId trackId, DeviceId deviceId) const;

    /** Fraction 0..1 of the running pass's capture window written so far.
        UI polls this while isFreezing(); completion lands as a model change
        (the device's freeze state), so no listener registration — slot UIs
        and this per-edit service have independent lifetimes. */
    double getActiveFreezeProgress() const;

    /** Start a real-time freeze pass for an External FX / Instrument device.
        Returns false with errorOut set when preconditions fail (no clips, no
        send/return configured, another pass running, ...). */
    bool startFreeze(TrackId trackId, DeviceId deviceId, juce::String& errorOut);

    /** Abort the running pass; the partial capture file is deleted. */
    void cancelFreeze();

    /** Restore a frozen track: delete the frozen clip, restore the stashed
        clips, restore bypass states, clear the device's freeze state. */
    bool unfreeze(TrackId trackId, DeviceId deviceId, juce::String& errorOut);

  private:
    struct ActivePass;

    void timerCallback() override;
    void finishPass(bool success, const juce::String& error);
    void applyFreezeResult(ActivePass& pass);

    tracktion::engine::Edit& edit_;
    AudioBridge& audioBridge_;
    std::unique_ptr<ActivePass> pass_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertFreezeService)
};

}  // namespace magda
