#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <vector>

#include "../../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

class TrackController;

class MidiInputRouter {
  public:
    MidiInputRouter(te::Engine& engine, te::Edit& edit, TrackController& trackController);

    te::VirtualMidiInputDevice* getQwertyMidiDevice();

    void enableAllMidiInputDevices();
    void setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId);
    juce::String getTrackMidiInput(TrackId trackId) const;
    bool setSessionSlotMidiRecordingTarget(TrackId trackId, int sceneIndex, bool enabled);

    void setSurfaceOnlyMidiInputPort(const juce::String& midiDeviceIdOrName);
    void clearSurfaceOnlyMidiInputPorts();

    void updateForSelection();
    void resyncAllInputMonitors();
    void onMidiDevicesAvailable();
    void applyPendingRoutes();
    void handlePlaybackContextTick();

  private:
    bool isSurfaceOnlyMidiInput(const juce::String& liveIdentifier,
                                const juce::String& liveName) const;
    void removeSurfaceOnlyMidiInputTargets();

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
