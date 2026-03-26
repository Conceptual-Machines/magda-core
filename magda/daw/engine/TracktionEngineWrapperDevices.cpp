#include "../audio/AudioBridge.hpp"
#include "TracktionEngineWrapper.hpp"

namespace magda {

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

    // Detect if the actual audio hardware device changed (e.g. MacBook speakers → M4)
    juce::String currentDeviceName;
    if (auto* device = dm.deviceManager.getCurrentAudioDevice())
        currentDeviceName = device->getName();

    bool deviceChanged = (!currentDeviceName.isEmpty() && lastKnownAudioDeviceName_.isNotEmpty() &&
                          currentDeviceName != lastKnownAudioDeviceName_);
    bool devicesAdded = (totalDevices > lastKnownDeviceCount_);

    if (deviceChanged) {
        DBG("Audio device changed: " << lastKnownAudioDeviceName_ << " -> " << currentDeviceName);

        // Reconfigure wave input/output devices for the new hardware channel layout.
        // Enable all hardware channels at JUCE level, then sync TE wave devices.
        if (auto* device = dm.deviceManager.getCurrentAudioDevice()) {
            auto setup = dm.deviceManager.getAudioDeviceSetup();
            setup.inputChannels.clear();
            setup.inputChannels.setRange(0, device->getInputChannelNames().size(), true);
            setup.outputChannels.clear();
            setup.outputChannels.setRange(0, device->getOutputChannelNames().size(), true);
            setup.useDefaultInputChannels = false;
            setup.useDefaultOutputChannels = false;
            dm.deviceManager.setAudioDeviceSetup(setup, true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(0);

            int numInputChannels = device->getInputChannelNames().size();
            for (auto* dev : dm.getWaveInputDevices()) {
                bool shouldEnable = false;
                for (const auto& ch : dev->getChannels()) {
                    if (ch.indexInDevice < numInputChannels) {
                        shouldEnable = true;
                        break;
                    }
                }
                dev->setEnabled(shouldEnable);
            }

            DBG("Reconfigured wave devices for " << currentDeviceName << " (" << numInputChannels
                                                 << " inputs)");
        }
    }

    if (devicesAdded || deviceChanged) {
        ctx->reallocate();
        int inputsAfter = static_cast<int>(ctx->getAllInputs().size());
        DBG("Device change: Reallocated playback context (inputs: " << inputsBefore << " -> "
                                                                    << inputsAfter << ")");
    }

    lastKnownDeviceCount_ = totalDevices;
    if (currentDeviceName.isNotEmpty())
        lastKnownAudioDeviceName_ = currentDeviceName;
}

void TracktionEngineWrapper::notifyDeviceLoadingComplete(const juce::String& message) {
    // If we were playing, stop and remember we need to resume
    if (isPlaying() && devicesLoading_) {
        wasPlayingBeforeDeviceChange_ = true;
        stop();
        DBG("Stopped playback during device initialization");
    }

    // Mark devices as no longer loading after first change notification
    if (devicesLoading_) {
        devicesLoading_ = false;
        DBG("Device initialization complete: " << message);

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
    if (engine_) {
        return &engine_->getDeviceManager().deviceManager;
    }
    return nullptr;
}

}  // namespace magda
