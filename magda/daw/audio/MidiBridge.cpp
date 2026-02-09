#include "MidiBridge.hpp"

#include "../core/TrackManager.hpp"
#include "AudioBridge.hpp"

namespace magda {

MidiBridge::MidiBridge(te::Engine& engine) : engine_(engine) {
    DBG("MidiBridge initialized");
}

void MidiBridge::setAudioBridge(AudioBridge* audioBridge) {
    audioBridge_ = audioBridge;
}

MidiBridge::~MidiBridge() {
    // IMPORTANT: Release MIDI inputs carefully to avoid CoreMIDI crashes
    // Don't hold lock while destroying MIDI inputs (can cause deadlock)
    std::unordered_map<juce::String, std::unique_ptr<juce::MidiInput>> inputsToDestroy;

    {
        juce::ScopedLock lock(routingLock_);
        inputsToDestroy = std::move(activeMidiInputs_);
        activeMidiInputs_.clear();
        trackMidiInputs_.clear();
        monitoredTracks_.clear();
    }

    // Destroy MIDI inputs outside of lock
    for (auto& [deviceId, midiInput] : inputsToDestroy) {
        if (midiInput) {
            midiInput->stop();
        }
    }
    inputsToDestroy.clear();
}

std::vector<MidiDeviceInfo> MidiBridge::getAvailableMidiInputs() const {
    std::vector<MidiDeviceInfo> devices;

    // Use JUCE's MidiInput::getAvailableDevices() instead of Tracktion's device manager
    // This works immediately without waiting for async scan
    auto midiInputs = juce::MidiInput::getAvailableDevices();

    for (const auto& device : midiInputs) {
        MidiDeviceInfo info;
        info.id = device.identifier;
        info.name = device.name;
        info.isEnabled = false;  // Input enable state handled separately
        info.isAvailable = true;

        devices.push_back(info);
    }

    return devices;
}

std::vector<MidiDeviceInfo> MidiBridge::getAvailableMidiOutputs() const {
    std::vector<MidiDeviceInfo> devices;

    auto midiOutputs = juce::MidiOutput::getAvailableDevices();

    for (const auto& device : midiOutputs) {
        MidiDeviceInfo info;
        info.id = device.identifier;
        info.name = device.name;
        info.isEnabled = false;  // Output enable state not tracked same way
        info.isAvailable = true;

        devices.push_back(info);
    }

    return devices;
}

void MidiBridge::enableMidiInput(const juce::String& deviceId) {
    juce::ScopedLock lock(routingLock_);

    // Check if already listening
    if (activeMidiInputs_.find(deviceId) != activeMidiInputs_.end()) {
        return;  // Already active
    }

    // Open MIDI input and start listening
    auto availableDevices = juce::MidiInput::getAvailableDevices();

    for (const auto& deviceInfo : availableDevices) {
        if (deviceInfo.identifier == deviceId) {
            auto midiInput = juce::MidiInput::openDevice(deviceInfo.identifier, this);
            if (midiInput) {
                midiInput->start();
                activeMidiInputs_[deviceId] = std::move(midiInput);
                DBG("Started MIDI activity monitoring for: " << deviceInfo.name);
            }
            break;
        }
    }
}

void MidiBridge::disableMidiInput(const juce::String& deviceId) {
    // Stop our MIDI input listener
    juce::ScopedLock lock(routingLock_);
    auto it = activeMidiInputs_.find(deviceId);
    if (it != activeMidiInputs_.end()) {
        if (it->second) {
            it->second->stop();
        }
        activeMidiInputs_.erase(it);
    }
}

bool MidiBridge::isMidiInputEnabled(const juce::String& deviceId) const {
    auto device = engine_.getDeviceManager().findMidiInputDeviceForID(deviceId);
    return device ? device->isEnabled() : false;
}

void MidiBridge::setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId) {
    juce::ScopedLock lock(routingLock_);

    DBG("MidiBridge::setTrackMidiInput - trackId=" << trackId << " midiDeviceId='" << midiDeviceId
                                                   << "'");

    if (midiDeviceId.isEmpty()) {
        // Clear routing
        trackMidiInputs_.erase(trackId);
        DBG("  -> Cleared routing for track " << trackId);
    } else {
        trackMidiInputs_[trackId] = midiDeviceId;
        DBG("  -> Stored routing: track " << trackId << " -> '" << midiDeviceId << "'");

        // Auto-enable the device if not already enabled
        if (midiDeviceId == "all") {
            // Special case: enable ALL MIDI input devices
            auto availableDevices = juce::MidiInput::getAvailableDevices();
            DBG("  -> 'all' mode: enabling " << availableDevices.size() << " MIDI input devices");
            for (const auto& deviceInfo : availableDevices) {
                enableMidiInput(deviceInfo.identifier);
            }
        } else {
            // Single device
            DBG("  -> Single device mode: enabling '" << midiDeviceId << "'");
            enableMidiInput(midiDeviceId);
        }
    }

    // Debug: print current routing state
    DBG("  -> Current track routings:");
    for (const auto& [tid, did] : trackMidiInputs_) {
        DBG("     track " << tid << " -> '" << did << "'");
    }
}

juce::String MidiBridge::getTrackMidiInput(TrackId trackId) const {
    juce::ScopedLock lock(routingLock_);

    auto it = trackMidiInputs_.find(trackId);
    if (it != trackMidiInputs_.end()) {
        return it->second;
    }
    return {};  // Empty string = no input
}

void MidiBridge::clearTrackMidiInput(TrackId trackId) {
    setTrackMidiInput(trackId, {});  // This will trigger the callback
}

void MidiBridge::handleIncomingMidiMessage(juce::MidiInput* source,
                                           const juce::MidiMessage& message) {
    if (!source) {
        return;
    }

    // Skip MIDI clock and other system messages for activity/routing
    if (message.isMidiClock() || message.isActiveSense() || message.isMidiStart() ||
        message.isMidiStop() || message.isMidiContinue()) {
        return;
    }

    // Get the device ID for this input
    juce::String sourceDeviceId = source->getIdentifier();

    // Push event to global queue for MIDI monitor
    {
        MidiEventEntry entry;
        entry.deviceName = source->getName();
        entry.channel = message.getChannel();
        entry.timestamp = juce::Time::getMillisecondCounterHiRes() / 1000.0;

        if (message.isNoteOn()) {
            entry.type = MidiEventEntry::NoteOn;
            entry.data1 = message.getNoteNumber();
            entry.data2 = message.getVelocity();
        } else if (message.isNoteOff()) {
            entry.type = MidiEventEntry::NoteOff;
            entry.data1 = message.getNoteNumber();
            entry.data2 = message.getVelocity();
        } else if (message.isController()) {
            entry.type = MidiEventEntry::CC;
            entry.data1 = message.getControllerNumber();
            entry.data2 = message.getControllerValue();
        } else if (message.isPitchWheel()) {
            entry.type = MidiEventEntry::PitchBend;
            entry.pitchBendValue = message.getPitchWheelValue();
        } else {
            entry.type = MidiEventEntry::Other;
        }

        globalEventQueue_.push(entry);
    }

    // Debug: Log MIDI message receipt
    if (message.isNoteOn()) {
        DBG("MidiBridge: Note ON received - note="
            << message.getNoteNumber() << " vel=" << message.getVelocity() << " from '"
            << source->getName() << "' (id='" << sourceDeviceId << "')"
            << " trackRoutings=" << trackMidiInputs_.size());
    }

    // Find all tracks routing this MIDI input
    juce::ScopedLock lock(routingLock_);

    for (const auto& [trackId, deviceId] : trackMidiInputs_) {
        // Check if this track is routing this device (or "all" inputs)
        bool matches = (deviceId == sourceDeviceId || deviceId == "all");

        if (message.isNoteOn()) {
            DBG("  -> Checking track " << trackId << ": routing='" << deviceId << "' vs source='"
                                       << sourceDeviceId << "' -> "
                                       << (matches ? "MATCH" : "no match"));
        }

        if (matches) {
            // NOTE: MIDI routing to plugins is now handled by Tracktion Engine's
            // native InputDeviceInstance -> MidiInputDeviceNode system.
            // MidiBridge only monitors MIDI activity for UI visualization.

            if (message.isNoteOn()) {
                DBG("MidiBridge: triggerMidiNoteOn for trackId=" << trackId);
                if (audioBridge_)
                    audioBridge_->triggerMidiActivity(trackId);
                TrackManager::getInstance().triggerMidiNoteOn(trackId);
            }

            // Check if monitoring is enabled for this track for callbacks
            if (monitoredTracks_.find(trackId) != monitoredTracks_.end()) {
                // Call callbacks if set (for note/CC monitoring)
                if (message.isNoteOn() || message.isNoteOff()) {
                    if (onNoteEvent) {
                        MidiNoteEvent noteEvent;
                        noteEvent.noteNumber = message.getNoteNumber();
                        noteEvent.velocity = message.getVelocity();
                        noteEvent.isNoteOn = message.isNoteOn();
                        onNoteEvent(trackId, noteEvent);
                    }
                } else if (message.isController()) {
                    if (onCCEvent) {
                        MidiCCEvent ccEvent;
                        ccEvent.controller = message.getControllerNumber();
                        ccEvent.value = message.getControllerValue();
                        onCCEvent(trackId, ccEvent);
                    }
                }
            }

            // Push note events to recording queue for real-time preview
            if (message.isNoteOn() || message.isNoteOff()) {
                auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
                if (recordingQueue_ && transportPosition_ && trackInfo && trackInfo->recordArmed) {
                    RecordingNoteEvent evt;
                    evt.trackId = trackId;
                    evt.noteNumber = message.getNoteNumber();
                    evt.velocity = message.getVelocity();
                    evt.isNoteOn = message.isNoteOn();
                    evt.transportSeconds = transportPosition_->load(std::memory_order_relaxed);
                    recordingQueue_->push(evt);
                    DBG("RecPreview::push: note=" << evt.noteNumber << " on=" << (int)evt.isNoteOn
                                                  << " t=" << evt.transportSeconds);
                }
            }
        }
    }
}

void MidiBridge::setRecordingQueue(RecordingNoteQueue* queue, std::atomic<double>* transportPos) {
    recordingQueue_ = queue;
    transportPosition_ = transportPos;
}

void MidiBridge::startMonitoring(TrackId trackId) {
    juce::ScopedLock lock(routingLock_);
    monitoredTracks_.insert(trackId);
}

void MidiBridge::stopMonitoring(TrackId trackId) {
    juce::ScopedLock lock(routingLock_);
    monitoredTracks_.erase(trackId);
    DBG("Stopped MIDI monitoring for track " << trackId);
}

bool MidiBridge::isMonitoring(TrackId trackId) const {
    juce::ScopedLock lock(routingLock_);
    return monitoredTracks_.find(trackId) != monitoredTracks_.end();
}

}  // namespace magda
