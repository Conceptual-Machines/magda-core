#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <vector>

#include "../../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

class TrackController;

class MidiInputRouter : private juce::AsyncUpdater {
  public:
    MidiInputRouter(te::Engine& engine, te::Edit& edit, TrackController& trackController);
    ~MidiInputRouter() override;

    te::VirtualMidiInputDevice* getQwertyMidiDevice();

    void enableAllMidiInputDevices();
    void setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId);
    juce::String getTrackMidiInput(TrackId trackId) const;
    bool setSessionSlotMidiRecordingTarget(TrackId trackId, int sceneIndex, bool enabled);

    void setSurfaceOnlyMidiInputPort(const juce::String& midiDeviceIdOrName);
    void clearSurfaceOnlyMidiInputPorts();

    void updateMidiInputRouting();

    /// Coalesced entry point for input-monitor changes. A real monitor-mode
    /// change on a te::InputDevice triggers TransportControl::
    /// restartAllTransports(), so bursts of monitor edits (e.g. rapid clicks
    /// draining in one message-loop pass) must collapse to a single
    /// application, and it must never run re-entrantly from inside a
    /// TrackManager notification or a graph-reallocation callback.
    void requestInputMonitorResync();

    /// Applies MAGDA per-track Monitor state to the TE input devices now.
    /// Prefer requestInputMonitorResync() unless synchronous application is
    /// genuinely required.
    void resyncAllInputMonitors();

    void onMidiDevicesAvailable();
    void applyPendingRoutes();
    void handlePlaybackContextTick();

  private:
    bool isSurfaceOnlyMidiInput(const juce::String& liveIdentifier,
                                const juce::String& liveName) const;
    void removeSurfaceOnlyMidiInputTargets();

    void handleAsyncUpdate() override;

    te::Engine& engine_;
    te::Edit& edit_;
    TrackController& trackController_;

    std::shared_ptr<te::MidiInputDevice> qwertyMidiDevice_;
    bool qwertyNeedsContextRefresh_ = false;

    juce::StringArray surfaceOnlyMidiInputPorts_;
    mutable juce::CriticalSection surfaceOnlyMidiInputLock_;

    std::vector<std::pair<TrackId, juce::String>> pendingMidiRoutes_;
    te::EditPlaybackContext* lastPlaybackContext_ = nullptr;
};

}  // namespace magda
