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
    stopAllInputs();

    // Tear down outputs after inputs so any in-flight controller-feedback
    // sends from listener callbacks have already drained.
    juce::ScopedLock lock(routingLock_);
    activeMidiOutputs_.clear();
}

void MidiBridge::stopAllInputs() {
    // Signal all callbacks to bail out immediately
    isShuttingDown_.store(true, std::memory_order_release);

    // Move inputs out from under the lock so we can stop/destroy outside it
    std::unordered_map<juce::String, std::unique_ptr<juce::MidiInput>> inputsToDestroy;

    {
        juce::ScopedLock lock(routingLock_);
        inputsToDestroy = std::move(activeMidiInputs_);
        activeMidiInputs_.clear();
        trackMidiInputs_.clear();
        monitoredTracks_.clear();
    }

    // Stop, then explicitly unregister this callback BEFORE destroying.
    //
    // MidiInput::~MidiInput leaves the WaitFreeListeners populated until the
    // Impl's members are destroyed; if a CoreMIDI callback thread is mid
    // WaitFreeListeners::call when MidiBridge gets freed, the in-flight
    // dispatch dereferences a freed `MidiInputCallback*` and crashes at
    // vtable offset 0x10. removeCallback() goes through
    // WaitFreeListeners::remove which spin-waits for any in-flight callback
    // to finish before returning, so after this loop no CoreMIDI thread
    // can be holding a pointer to *this.
    for (auto& [deviceId, midiInput] : inputsToDestroy) {
        if (midiInput) {
            midiInput->stop();
            midiInput->removeCallback(*this);
        }
    }
    inputsToDestroy.clear();

    // Wait for any in-flight callbacks that entered before shutdown flag was set
    while (activeCallbacks_.load(std::memory_order_acquire) > 0)
        juce::Thread::sleep(1);
}

std::vector<MidiDeviceInfo> MidiBridge::getAvailableMidiInputs() const {
    std::vector<MidiDeviceInfo> devices;

    // Use JUCE's MidiInput::getAvailableDevices() for physical MIDI devices
    auto midiInputs = juce::MidiInput::getAvailableDevices();

    for (const auto& device : midiInputs) {
        MidiDeviceInfo info;
        info.id = device.identifier;
        info.name = device.name;
        info.isEnabled = false;
        info.isAvailable = true;
        devices.push_back(info);
    }

    // Include TE virtual MIDI devices only when enabled. The routing
    // selectors refresh via onMidiDeviceListChanged when the device
    // state changes, so the filter is effective.
    for (auto& dev : engine_.getDeviceManager().getMidiInDevices()) {
        if (dynamic_cast<te::VirtualMidiInputDevice*>(dev.get()) && dev->isEnabled()) {
            MidiDeviceInfo info;
            info.id = dev->getDeviceID();
            info.name = dev->getName();
            info.isEnabled = dev->isEnabled();
            info.isAvailable = true;
            devices.push_back(info);
        }
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

bool MidiBridge::sendMidi(const juce::String& deviceNameOrId, const juce::MidiMessage& msg) {
    if (isShuttingDown_.load(std::memory_order_acquire))
        return false;

    juce::ScopedLock lock(routingLock_);

    // Re-check under the lock; teardown sets the flag while holding it.
    if (isShuttingDown_.load(std::memory_order_acquire))
        return false;

    // Resolve to the JUCE identifier so the cache key is canonical regardless
    // of whether the caller passed a display name or an identifier.
    auto available = juce::MidiOutput::getAvailableDevices();
    juce::String identifier;
    for (const auto& d : available) {
        if (d.identifier == deviceNameOrId || d.name == deviceNameOrId) {
            identifier = d.identifier;
            break;
        }
    }
    if (identifier.isEmpty())
        return false;

    auto it = activeMidiOutputs_.find(identifier);
    if (it == activeMidiOutputs_.end()) {
        auto out = juce::MidiOutput::openDevice(identifier);
        if (!out)
            return false;
        it = activeMidiOutputs_.emplace(identifier, std::move(out)).first;
    }

    it->second->sendMessageNow(msg);
    return true;
}

bool MidiBridge::sendSysEx(const juce::String& deviceNameOrId, const juce::uint8* data,
                           size_t numBytes) {
    return sendMidi(deviceNameOrId,
                    juce::MidiMessage::createSysExMessage(data, static_cast<int>(numBytes)));
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
    std::unique_ptr<juce::MidiInput> inputToDestroy;
    {
        juce::ScopedLock lock(routingLock_);
        auto it = activeMidiInputs_.find(deviceId);
        if (it != activeMidiInputs_.end()) {
            inputToDestroy = std::move(it->second);
            activeMidiInputs_.erase(it);
        }
    }
    if (inputToDestroy) {
        inputToDestroy->stop();
        // Drain any in-flight CoreMIDI dispatch holding a pointer to *this
        // before the unique_ptr destructor tears the MidiInput down.
        // See stopAllInputs for the full rationale.
        inputToDestroy->removeCallback(*this);
    }
    // inputToDestroy destroyed here, outside lock
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
    if (isShuttingDown_.load(std::memory_order_acquire))
        return;
    activeCallbacks_.fetch_add(1, std::memory_order_acq_rel);
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        activeCallbacks_.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    if (!source) {
        activeCallbacks_.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    // Skip MIDI clock and other system messages for activity/routing
    if (message.isMidiClock() || message.isActiveSense() || message.isMidiStart() ||
        message.isMidiStop() || message.isMidiContinue()) {
        activeCallbacks_.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    // Get both identifier (OS-native, stable per-machine) and display name
    // (stable across machines / OSes) for this input — listeners use both via
    // magda::midi::matches to support name-based fallback.
    juce::String sourceDeviceId = source->getIdentifier();
    juce::String sourceDeviceName = source->getName();

    // Notify raw MIDI listeners FIRST (before routing logic).
    // These run on the MIDI callback thread; implementations must be lock-free.
    {
        juce::ScopedLock lock(rawMidiListenersLock_);
        for (auto* listener : rawMidiListeners_)
            if (listener)
                listener->onRawMidi(sourceDeviceId, sourceDeviceName, message);
    }

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

    // Find all tracks routing this MIDI input
    juce::ScopedLock lock(routingLock_);

    for (const auto& [trackId, deviceId] : trackMidiInputs_) {
        // Check if this track is routing this device (or "all" inputs)
        bool matches = (deviceId == sourceDeviceId || deviceId == "all");

        if (matches) {
            // NOTE: MIDI routing to plugins is now handled by Tracktion Engine's
            // native InputDeviceInstance -> MidiInputDeviceNode system.
            // MidiBridge only monitors MIDI activity for UI visualization.

            if (message.isNoteOn()) {
                if (audioBridge_)
                    audioBridge_->triggerMidiActivity(trackId);
                TrackManager::getInstance().triggerMidiNoteOn(trackId);
            } else if (message.isNoteOff()) {
                TrackManager::getInstance().triggerMidiNoteOff(trackId);
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

    activeCallbacks_.fetch_sub(1, std::memory_order_acq_rel);
}

void MidiBridge::setRecordingQueue(RecordingNoteQueue* queue, std::atomic<double>* transportPos) {
    recordingQueue_ = queue;
    transportPosition_ = transportPos;
}

void MidiBridge::addRawMidiListener(RawMidiListener* listener) {
    juce::ScopedLock lock(rawMidiListenersLock_);
    rawMidiListeners_.addIfNotAlreadyThere(listener);
}

void MidiBridge::removeRawMidiListener(RawMidiListener* listener) {
    juce::ScopedLock lock(rawMidiListenersLock_);
    rawMidiListeners_.removeAllInstancesOf(listener);
}

void MidiBridge::broadcastSynthesizedNote(const juce::String& sourceDeviceId, int noteNumber,
                                          int velocity, bool isNoteOn) {
    juce::ScopedLock lock(routingLock_);

    for (const auto& [trackId, deviceId] : trackMidiInputs_) {
        const bool matches = (deviceId == sourceDeviceId || deviceId == "all");
        if (!matches)
            continue;

        // UI activity fires regardless of arm — same as physical MIDI.
        if (isNoteOn) {
            if (audioBridge_)
                audioBridge_->triggerMidiActivity(trackId);
            TrackManager::getInstance().triggerMidiNoteOn(trackId);
        } else {
            TrackManager::getInstance().triggerMidiNoteOff(trackId);
        }

        // Preview queue push is armed-only.
        if (!recordingQueue_ || !transportPosition_)
            continue;
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        if (!trackInfo || !trackInfo->recordArmed)
            continue;

        RecordingNoteEvent evt;
        evt.trackId = trackId;
        evt.noteNumber = noteNumber;
        evt.velocity = velocity;
        evt.isNoteOn = isNoteOn;
        evt.transportSeconds = transportPosition_->load(std::memory_order_relaxed);
        recordingQueue_->push(evt);
    }
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
