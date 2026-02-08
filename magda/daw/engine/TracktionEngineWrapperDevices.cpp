#include "TracktionEngineWrapper.hpp"

#include <iostream>

#include "../audio/AudioBridge.hpp"
#include "../core/DeviceInfo.hpp"

namespace magda {

// =============================================================================
// PDC Query Methods
// =============================================================================

double TracktionEngineWrapper::getPluginLatencySeconds(const std::string& effect_id) const {
    // TODO: Implement when we have effect tracking
    // For now, iterate all tracks and their plugins to find by ID
    juce::ignoreUnused(effect_id);
    return 0.0;
}

double TracktionEngineWrapper::getGlobalLatencySeconds() const {
    if (!currentEdit_) {
        return 0.0;
    }

    // Get the playback context which contains the audio graph
    auto* context = currentEdit_->getCurrentPlaybackContext();
    if (!context) {
        return 0.0;
    }

    // Tracktion Engine calculates PDC automatically and stores max latency
    // The easiest approach is to iterate all tracks and find max plugin latency
    double maxLatency = 0.0;

    for (auto* track : currentEdit_->getTrackList()) {
        if (auto* audioTrack = dynamic_cast<tracktion::AudioTrack*>(track)) {
            // Check all plugins on this track
            for (auto* plugin : audioTrack->pluginList) {
                // Use base Plugin class method (works for all plugin types)
                maxLatency = std::max(maxLatency, plugin->getLatencySeconds());
            }
        }
    }

    // Add device latency
    maxLatency += engine_->getDeviceManager().getOutputLatencySeconds();

    return maxLatency;
}

// =============================================================================
// Change Listener
// =============================================================================

void TracktionEngineWrapper::handleMidiDeviceChanges(tracktion::DeviceManager& dm) {
    auto midiDevices = dm.getMidiInDevices();
    DBG("Device change callback: " << midiDevices.size() << " MIDI input devices");

    // Enable any new MIDI input devices that have appeared
    for (auto& midiIn : midiDevices) {
        if (midiIn && !midiIn->isEnabled()) {
            midiIn->setEnabled(true);
            DBG("Device change: Enabled MIDI input: " << midiIn->getName());
        }
    }

    // Notify AudioBridge that MIDI devices are now available
    bool hasMidiDevices = !midiDevices.empty();
    if (hasMidiDevices && audioBridge_) {
        audioBridge_->onMidiDevicesAvailable();
    }
}

void TracktionEngineWrapper::handlePlaybackContextReallocation(tracktion::DeviceManager& dm) {
    if (!currentEdit_) {
        return;
    }

    auto* ctx = currentEdit_->getCurrentPlaybackContext();
    if (!ctx) {
        return;
    }

    int inputsBefore = static_cast<int>(ctx->getAllInputs().size());

    // Count current available devices to detect additions
    int totalDevices = static_cast<int>(dm.getMidiInDevices().size()) +
                       static_cast<int>(dm.getWaveInputDevices().size()) +
                       static_cast<int>(dm.getWaveOutputDevices().size());

    if (totalDevices > lastKnownDeviceCount_) {
        ctx->reallocate();
        int inputsAfter = static_cast<int>(ctx->getAllInputs().size());
        DBG("Device change: Reallocated playback context (inputs: " << inputsBefore << " -> "
                                                                    << inputsAfter << ")");
    }
    lastKnownDeviceCount_ = totalDevices;
}

void TracktionEngineWrapper::notifyDeviceLoadingComplete(const juce::String& message) {
    // If we were playing, stop and remember we need to resume
    if (isPlaying() && devicesLoading_) {
        wasPlayingBeforeDeviceChange_ = true;
        stop();
        std::cout << "Stopped playback during device initialization" << std::endl;
    }

    // Mark devices as no longer loading after first change notification
    if (devicesLoading_) {
        devicesLoading_ = false;
        std::cout << "Device initialization complete: " << message << std::endl;

        if (onDevicesLoadingChanged) {
            onDevicesLoadingChanged(false, message);
        }
    }
}

void TracktionEngineWrapper::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // DeviceManager changed - this happens during MIDI device scanning
    if (!engine_ || source != &engine_->getDeviceManager()) {
        return;
    }

    auto& dm = engine_->getDeviceManager();

    // Enable MIDI devices and notify AudioBridge
    handleMidiDeviceChanges(dm);

    // Reallocate playback context if devices were added
    handlePlaybackContextReallocation(dm);

    // Build a description of currently enabled devices
    juce::StringArray deviceNames;

    // Get MIDI input devices (returns shared_ptr)
    for (const auto& midiIn : dm.getMidiInDevices()) {
        if (midiIn && midiIn->isEnabled()) {
            deviceNames.add("MIDI: " + midiIn->getName());
        }
    }

    // Get audio output device (returns raw pointers)
    for (auto* waveOut : dm.getWaveOutputDevices()) {
        if (waveOut && waveOut->isEnabled()) {
            deviceNames.add("Audio: " + waveOut->getName());
        }
    }

    juce::String message;
    if (devicesLoading_) {
        message = "Scanning devices...";
        if (deviceNames.size() > 0) {
            message = "Found: " + deviceNames.joinIntoString(", ");
        }
    } else {
        message = "Devices ready";
    }

    // Notify completion and stop playback if needed
    notifyDeviceLoadingComplete(message);
}

juce::AudioDeviceManager* TracktionEngineWrapper::getDeviceManager() {
    if (!engine_) {
        return nullptr;
    }

    return &engine_->getDeviceManager().deviceManager;
}

}  // namespace magda
