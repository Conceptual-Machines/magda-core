#include "midi/MidiInputRouter.hpp"

#include <functional>
#include <unordered_set>

#include "../../core/RackInfo.hpp"
#include "../../core/TrackManager.hpp"
#include "../MidiBridge.hpp"
#include "TrackController.hpp"
#include "midi/MidiDeviceMatch.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"

namespace magda {

namespace {

te::InputDevice::MonitorMode toTeMonitorMode(InputMonitorMode mode) {
    switch (mode) {
        case InputMonitorMode::In:
            return te::InputDevice::MonitorMode::on;
        case InputMonitorMode::Auto:
            return te::InputDevice::MonitorMode::automatic;
        case InputMonitorMode::Off:
        default:
            return te::InputDevice::MonitorMode::off;
    }
}

te::MidiInputDevice* getLiveMidiInputDevice(te::Engine& engine,
                                            te::InputDeviceInstance* inputDeviceInstance) {
    if (!inputDeviceInstance)
        return nullptr;

    auto* owner = &inputDeviceInstance->owner;
    const auto matchesOwner = [owner](const auto& midiInput) {
        return midiInput && midiInput.get() == owner;
    };
    const auto midiInputs = engine.getDeviceManager().getMidiInDevices();
    const auto found = std::ranges::find_if(midiInputs, matchesOwner);
    return found == midiInputs.end() ? nullptr : found->get();
}

te::MidiInputDevice* getLiveTrackMidiInputDevice(TrackController& trackController,
                                                 te::InputDeviceInstance* inputDeviceInstance,
                                                 TrackId* sourceTrackId = nullptr) {
    if (!inputDeviceInstance)
        return nullptr;

    auto* owner = &inputDeviceInstance->owner;
    te::MidiInputDevice* result = nullptr;
    trackController.withTrackMapping([&](const auto& mapping) {
        const auto matchesOwner = [owner](const auto& entry) {
            return entry.second && &entry.second->getMidiInputDevice() == owner;
        };
        if (const auto found = std::ranges::find_if(mapping, matchesOwner);
            found != mapping.end()) {
            if (sourceTrackId)
                *sourceTrackId = found->first;
            result = &found->second->getMidiInputDevice();
        }
    });

    return result;
}

te::InputDevice* getLiveInputDevice(te::Engine& engine,
                                    te::InputDeviceInstance* inputDeviceInstance) {
    if (auto* midiInput = getLiveMidiInputDevice(engine, inputDeviceInstance))
        return midiInput;

    if (!inputDeviceInstance)
        return nullptr;

    auto* owner = &inputDeviceInstance->owner;
    const auto matchesOwner = [owner](auto* waveInput) { return waveInput == owner; };
    const auto waveInputs = engine.getDeviceManager().getWaveInputDevices();
    const auto found = std::ranges::find_if(waveInputs, matchesOwner);
    return found == waveInputs.end() ? nullptr : *found;
}

te::WaveInputDevice* getLiveTrackWaveInputDevice(TrackController& trackController,
                                                 te::InputDeviceInstance* inputDeviceInstance) {
    if (!inputDeviceInstance)
        return nullptr;

    auto* owner = &inputDeviceInstance->owner;
    te::WaveInputDevice* result = nullptr;
    trackController.withTrackMapping([&](const auto& mapping) {
        const auto matchesOwner = [owner](const auto& entry) {
            return entry.second && &entry.second->getWaveInputDevice() == owner;
        };
        if (const auto found = std::ranges::find_if(mapping, matchesOwner); found != mapping.end())
            result = &found->second->getWaveInputDevice();
    });

    return result;
}

}  // namespace

MidiInputRouter::MidiInputRouter(te::Engine& engine, te::Edit& edit,
                                 TrackController& trackController)
    : engine_(engine), edit_(edit), trackController_(trackController) {}

MidiInputRouter::~MidiInputRouter() {
    cancelPendingUpdate();

    // Unhook preview consumers from any live input instances before the
    // consumer objects are destroyed. removeConsumer takes the same lock the
    // audio-thread callback iteration holds, so after this no callback can be
    // touching them.
    if (auto* playbackContext = edit_.getCurrentPlaybackContext()) {
        for (auto* inputDeviceInstance : playbackContext->getAllInputs())
            for (auto& [sourceTrackId, consumer] : trackMidiPreviewConsumers_)
                if (consumer)
                    inputDeviceInstance->removeConsumer(consumer.get());
    }
}

void MidiInputRouter::setRecordingQueue(RecordingNoteQueue* queue,
                                        std::atomic<double>* transportPosition) {
    recordingQueue_ = queue;
    transportPositionForMidi_ = transportPosition;
    for (auto& [sourceTrackId, consumer] : trackMidiPreviewConsumers_)
        if (consumer)
            consumer->configure(queue, transportPosition);
}

TrackId MidiInputRouter::resolveTargetTrackId(te::EditItemID targetID) const {
    TrackId result = INVALID_TRACK_ID;
    trackController_.withTrackMapping([&](const auto& mapping) {
        const auto matchesTarget = [targetID](const auto& entry) {
            if (!entry.second)
                return false;
            if (entry.second->itemID == targetID)
                return true;
            // Session-slot recording targets the slot, not the track.
            const auto matchesSlot = [targetID](auto* slot) {
                return slot && slot->itemID == targetID;
            };
            return std::ranges::any_of(entry.second->getClipSlotList().getClipSlots(), matchesSlot);
        };
        if (const auto found = std::ranges::find_if(mapping, matchesTarget); found != mapping.end())
            result = found->first;
    });
    return result;
}

void MidiInputRouter::syncTrackMidiPreviewConsumers() {
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext)
        return;

    auto& tm = TrackManager::getInstance();

    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        TrackId sourceTrackId = INVALID_TRACK_ID;
        if (!getLiveTrackMidiInputDevice(trackController_, inputDeviceInstance, &sourceTrackId))
            continue;

        // Preview events are only wanted for armed destinations (mirrors
        // MidiBridge's recordArmed gate), so un-armed monitoring doesn't fill
        // the queue with events nobody drains.
        std::vector<TrackId> armedTargets;
        for (auto targetID : inputDeviceInstance->getTargets()) {
            const TrackId destTrackId = resolveTargetTrackId(targetID);
            if (destTrackId == INVALID_TRACK_ID)
                continue;
            const auto* destInfo = tm.getTrack(destTrackId);
            if (!destInfo || !destInfo->recordArmed)
                continue;
            if (std::ranges::find(armedTargets, destTrackId) == armedTargets.end())
                armedTargets.push_back(destTrackId);
        }

        auto& consumer = trackMidiPreviewConsumers_[sourceTrackId];
        if (!consumer)
            consumer = std::make_unique<TrackMidiPreviewConsumer>();

        consumer->configure(recordingQueue_, transportPositionForMidi_);
        consumer->setArmedTargets(armedTargets);

        // addConsumer is idempotent (addIfNotAlreadyThere), so re-adding every
        // sync keeps registration alive across playback-context/instance
        // recreation (restartAllTransports, device list rebuilds).
        if (armedTargets.empty())
            inputDeviceInstance->removeConsumer(consumer.get());
        else
            inputDeviceInstance->addConsumer(consumer.get());
    }
}

void MidiInputRouter::requestInputMonitorResync() {
    triggerAsyncUpdate();
}

void MidiInputRouter::handleAsyncUpdate() {
    resyncAllInputMonitors();
}

te::VirtualMidiInputDevice* MidiInputRouter::getQwertyMidiDevice() {
    // Only accept actual VirtualMidiInputDevice instances — a physical device
    // with the same name would break the cast and leave the feature silently
    // disabled.
    const auto isQwertyDevice = [](const auto& dev) {
        return dev->getName() == kQwertyMidiDeviceName &&
               dynamic_cast<te::VirtualMidiInputDevice*>(dev.get()) != nullptr;
    };

    if (!qwertyMidiDevice_) {
        // Check if it already exists (persisted from a previous session).
        const auto midiInputs = engine_.getDeviceManager().getMidiInDevices();
        if (const auto found = std::ranges::find_if(midiInputs, isQwertyDevice);
            found != midiInputs.end())
            qwertyMidiDevice_ = *found;

        if (!qwertyMidiDevice_) {
            auto result = engine_.getDeviceManager().createVirtualMidiDevice(kQwertyMidiDeviceName);
            if (result.wasOk()) {
                const auto newMidiInputs = engine_.getDeviceManager().getMidiInDevices();
                if (const auto found = std::ranges::find_if(newMidiInputs, isQwertyDevice);
                    found != newMidiInputs.end())
                    qwertyMidiDevice_ = *found;
                if (qwertyMidiDevice_)
                    qwertyNeedsContextRefresh_ = true;
            } else {
                DBG("Failed to create QWERTY virtual MIDI device: " << result.getErrorMessage());
            }
        }

        if (qwertyMidiDevice_)
            DBG("QWERTY virtual MIDI device ready");
    }

    if (qwertyNeedsContextRefresh_) {
        if (auto* ctx = edit_.getCurrentPlaybackContext(); ctx && ctx->isPlaybackGraphAllocated()) {
            ctx->reallocate();
            qwertyNeedsContextRefresh_ = false;
        }
    }

    return dynamic_cast<te::VirtualMidiInputDevice*>(qwertyMidiDevice_.get());
}

void MidiInputRouter::enableAllMidiInputDevices() {
    auto& dm = engine_.getDeviceManager();

    for (auto& midiInput : dm.getMidiInDevices()) {
        if (!midiInput)
            continue;
        if (!midiInput->isEnabled()) {
            midiInput->setEnabled(true);
            DBG("Enabled MIDI input device: " << midiInput->getName());
        }
    }

    DBG("All MIDI input devices enabled in Tracktion Engine");
}

bool MidiInputRouter::isSurfaceOnlyMidiInput(const juce::String& liveIdentifier,
                                             const juce::String& liveName) const {
    juce::StringArray keys;
    {
        juce::ScopedLock lock(surfaceOnlyMidiInputLock_);
        keys = surfaceOnlyMidiInputPorts_;
    }

    const auto matchesLive = [&](const juce::String& key) {
        return magda::midi::matches(key, liveIdentifier, liveName);
    };
    return std::ranges::any_of(keys, matchesLive);
}

void MidiInputRouter::removeSurfaceOnlyMidiInputTargets() {
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext)
        return;

    bool removedAnyRouting = false;
    auto& tm = TrackManager::getInstance();

    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        if (auto* midiDevice = getLiveMidiInputDevice(engine_, inputDeviceInstance)) {
            if (!isSurfaceOnlyMidiInput(midiDevice->getDeviceID(), midiDevice->getName()))
                continue;

            for (const auto& trackInfo : tm.getTracks()) {
                auto* track = trackController_.getAudioTrack(trackInfo.id);
                if (!track)
                    continue;

                auto result = inputDeviceInstance->removeTarget(track->itemID, nullptr);
                if (result)
                    removedAnyRouting = true;
            }
        }
    }

    if (removedAnyRouting && playbackContext->isPlaybackGraphAllocated())
        playbackContext->reallocate();
}

bool MidiInputRouter::isExternalInstrumentSendbackInput(TrackId trackId,
                                                        const juce::String& inputName) {
    // Arm-gated: a record-armed track re-admits the synth's own ports so its
    // keyboard can be captured (Local Control off on the synth is the no-
    // double-trigger companion). Un-armed, the ports stay dropped so playback
    // through the insert can't double or loop back.
    if (const auto* track = TrackManager::getInstance().getTrack(trackId);
        track != nullptr && track->recordArmed)
        return false;

    auto routing = TrackManager::getInstance().getExternalInstrumentRouting(trackId);
    return routing.present && routing.midiOut.isNotEmpty() &&
           midi::sameMidiHardware(inputName, routing.midiOut);
}

void MidiInputRouter::reapplyExternalInstrumentSendbackGuard(TrackId trackId) {
    // Re-apply the track's MIDI route so a changed send target drops the new
    // port and readmits the previous one. The model's configured value ("all" /
    // device id) is authoritative; tracks routed automatically by
    // updateMidiInputRouting have an empty model value but are always in "all"
    // mode.
    juce::String route;
    if (auto* info = TrackManager::getInstance().getTrack(trackId))
        route = info->midiInputDevice;
    if (route.isEmpty() && getTrackMidiInput(trackId).isNotEmpty())
        route = "all";
    if (route.isEmpty() || route.startsWith("track:"))
        return;  // no hardware route to guard
    setTrackMidiInput(trackId, route);
}

void MidiInputRouter::setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId) {
    auto* track = trackController_.getAudioTrack(trackId);
    if (!track)
        return;

    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext) {
        pendingMidiRoutes_.emplace_back(trackId, midiDeviceId);
        return;
    }

    if (midiDeviceId.isEmpty()) {
        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            if (getLiveMidiInputDevice(engine_, inputDeviceInstance) ||
                getLiveTrackMidiInputDevice(trackController_, inputDeviceInstance)) [[maybe_unused]]
                auto result = inputDeviceInstance->removeTarget(track->itemID, nullptr);
        }
    } else if (midiDeviceId.startsWith("track:")) {
        // Route another track's MIDI output as input (internal routing)
        TrackId sourceTrackId =
            midiDeviceId.fromFirstOccurrenceOf("track:", false, false).getIntValue();
        auto* sourceTrack = trackController_.getAudioTrack(sourceTrackId);

        // Clear existing MIDI input targets first (hardware + track MIDI) so
        // switching inputs doesn't accumulate targets
        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            if (getLiveMidiInputDevice(engine_, inputDeviceInstance) ||
                getLiveTrackMidiInputDevice(trackController_, inputDeviceInstance)) [[maybe_unused]]
                auto result = inputDeviceInstance->removeTarget(track->itemID, nullptr);
        }

        if (sourceTrack) {
            auto* dest =
                te::assignTrackAsInput(*track, *sourceTrack, te::InputDevice::trackMidiDevice);
            if (dest) {
                dest->recordEnabled = false;  // Arming happens separately

                // te::assignTrackAsInput leaves the source track's MIDI device
                // disabled until TE's async trackDeviceEnabler runs, and the
                // device defaults to MonitorMode::automatic (thru only while
                // recording). The reallocate below would then build a graph
                // without the source-side tap and with monitoring off — enable
                // the device and apply the destination's monitor mode first.
                auto& sourceDevice = sourceTrack->getMidiInputDevice();
                if (!sourceDevice.isEnabled())
                    sourceDevice.setEnabled(true);
                auto teMonitorMode = te::InputDevice::MonitorMode::on;
                if (auto* destInfo = TrackManager::getInstance().getTrack(trackId))
                    teMonitorMode = toTeMonitorMode(destInfo->inputMonitor);
                sourceDevice.setMonitorMode(teMonitorMode);

                DBG("MidiInputRouter: assigned track " << sourceTrackId << " as MIDI input for "
                                                       << trackId);
            } else {
                DBG("MidiInputRouter: assignTrackAsInput returned null for source track "
                    << sourceTrackId);
            }
        } else {
            DBG("MidiInputRouter: source track not found: " << sourceTrackId);
        }

        if (playbackContext->isPlaybackGraphAllocated())
            playbackContext->reallocate();
    } else if (midiDeviceId == "all") {
        bool addedAnyRouting = false;
        bool removedAnyRouting = false;

        auto teMonitorMode = te::InputDevice::MonitorMode::on;
        if (auto* trackInfo = TrackManager::getInstance().getTrack(trackId))
            teMonitorMode = toTeMonitorMode(trackInfo->inputMonitor);

        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            if (auto* midiDevice = getLiveMidiInputDevice(engine_, inputDeviceInstance)) {
                if (midiDevice->getName() == "All MIDI Ins")
                    continue;

                if (isSurfaceOnlyMidiInput(midiDevice->getDeviceID(), midiDevice->getName()) ||
                    isExternalInstrumentSendbackInput(trackId, midiDevice->getName())) {
                    auto result = inputDeviceInstance->removeTarget(track->itemID, nullptr);
                    if (result)
                        removedAnyRouting = true;
                    continue;
                }

                if (!midiDevice->isEnabled())
                    midiDevice->setEnabled(true);

                midiDevice->setMonitorMode(teMonitorMode);

                auto result = inputDeviceInstance->setTarget(track->itemID, true, nullptr);
                if (result.has_value()) {
                    (*result)->recordEnabled = false;
                    addedAnyRouting = true;
                }
            }
        }

        if ((addedAnyRouting || removedAnyRouting) && playbackContext->isPlaybackGraphAllocated())
            playbackContext->reallocate();
    } else {
        auto& dm = engine_.getDeviceManager();
        bool addedRouting = false;
        te::MidiInputDevice* midiDevice = nullptr;

        if (auto dev = dm.findMidiInputDeviceForID(midiDeviceId)) {
            midiDevice = dev.get();
        } else {
            auto juceDevices = juce::MidiInput::getAvailableDevices();
            juce::String deviceName;
            const auto matchesIdentifier = [&midiDeviceId](const auto& d) {
                return d.identifier == midiDeviceId;
            };
            if (auto* const found = std::ranges::find_if(juceDevices, matchesIdentifier);
                found != juceDevices.end())
                deviceName = found->name;

            if (deviceName.isNotEmpty()) {
                const auto matchesName = [&deviceName](const auto& device) {
                    return device && device->getName() == deviceName;
                };
                const auto midiInputs = dm.getMidiInDevices();
                if (const auto found = std::ranges::find_if(midiInputs, matchesName);
                    found != midiInputs.end())
                    midiDevice = found->get();
            }
        }

        if (midiDevice) {
            // The sendback guard does NOT apply here: explicitly picking the
            // synth's own port is the record-its-keyboard workflow (Local
            // Control off on the synth); only "All Inputs" filters it out.
            if (isSurfaceOnlyMidiInput(midiDevice->getDeviceID(), midiDevice->getName())) {
                bool removedAnyRouting = false;
                const auto matchesOwner = [midiDevice](auto* inputDeviceInstance) {
                    return &inputDeviceInstance->owner == midiDevice;
                };
                const auto allInputs = playbackContext->getAllInputs();
                if (const auto* const found = std::ranges::find_if(allInputs, matchesOwner);
                    found != allInputs.end())
                    removedAnyRouting = (*found)->removeTarget(track->itemID, nullptr);
                if (removedAnyRouting && playbackContext->isPlaybackGraphAllocated())
                    playbackContext->reallocate();
                return;
            }

            if (!midiDevice->isEnabled())
                midiDevice->setEnabled(true);

            auto teMonitorModeSpecific = te::InputDevice::MonitorMode::on;
            if (auto* trackInfo2 = TrackManager::getInstance().getTrack(trackId))
                teMonitorModeSpecific = toTeMonitorMode(trackInfo2->inputMonitor);
            midiDevice->setMonitorMode(teMonitorModeSpecific);

            const auto matchesOwner = [midiDevice](auto* inputDeviceInstance) {
                return &inputDeviceInstance->owner == midiDevice;
            };
            const auto allInputs = playbackContext->getAllInputs();
            if (const auto* const found = std::ranges::find_if(allInputs, matchesOwner);
                found != allInputs.end()) {
                auto result = (*found)->setTarget(track->itemID, true, nullptr);
                if (result.has_value()) {
                    (*result)->recordEnabled = false;
                    addedRouting = true;
                }
            }
        }

        if (addedRouting && playbackContext->isPlaybackGraphAllocated())
            playbackContext->reallocate();
    }

    syncTrackMidiPreviewConsumers();
}

bool MidiInputRouter::setSessionSlotMidiRecordingTarget(TrackId trackId, int sceneIndex,
                                                        bool enabled) {
    auto* track = trackController_.getAudioTrack(trackId);
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!track || !playbackContext || !trackInfo || sceneIndex < 0)
        return false;

    edit_.getSceneList().ensureNumberOfScenes(sceneIndex + 1);
    track->getClipSlotList().ensureNumberOfSlots(sceneIndex + 1);

    auto slots = track->getClipSlotList().getClipSlots();
    if (sceneIndex >= slots.size() || slots[sceneIndex] == nullptr)
        return false;

    auto* slot = slots[sceneIndex];
    bool changedRouting = false;
    bool armedSlot = false;

    auto shouldUseDevice = [this, trackId, configured = trackInfo->midiInputDevice](
                               const te::MidiInputDevice& midiDevice) {
        if (configured.isEmpty())
            return false;
        if (midiDevice.getName() == "All MIDI Ins")
            return false;
        if (isSurfaceOnlyMidiInput(midiDevice.getDeviceID(), midiDevice.getName()))
            return false;
        // No sendback guard here: arming a session slot is an explicit
        // capture intent, so the synth's own ports stay recordable.
        if (configured == "all")
            return true;
        return magda::midi::matches(configured, midiDevice.getDeviceID(), midiDevice.getName());
    };

    const auto teMonitorMode = toTeMonitorMode(trackInfo->inputMonitor);

    // Internal track routing: the usable instance is the configured source
    // track's MIDI input device rather than a hardware device
    const bool usesTrackMidiInput = trackInfo->midiInputDevice.startsWith("track:");
    const TrackId configuredSourceId =
        usesTrackMidiInput ? static_cast<TrackId>(trackInfo->midiInputDevice
                                                      .fromFirstOccurrenceOf("track:", false, false)
                                                      .getIntValue())
                           : INVALID_TRACK_ID;

    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        auto* midiDevice = getLiveMidiInputDevice(engine_, inputDeviceInstance);
        if (usesTrackMidiInput) {
            TrackId sourceTrackId = INVALID_TRACK_ID;
            auto* trackMidiDevice =
                getLiveTrackMidiInputDevice(trackController_, inputDeviceInstance, &sourceTrackId);
            if (!trackMidiDevice || sourceTrackId != configuredSourceId)
                continue;
            midiDevice = trackMidiDevice;
        } else if (!midiDevice || !shouldUseDevice(*midiDevice)) {
            continue;
        }

        if (enabled) {
            if (!midiDevice->isEnabled())
                midiDevice->setEnabled(true);

            midiDevice->setMonitorMode(teMonitorMode);

            const auto matchesSlotTarget = [&](auto targetID) { return targetID == slot->itemID; };
            const bool hadSlotTarget =
                std::ranges::any_of(inputDeviceInstance->getTargets(), matchesSlotTarget);

            if (!hadSlotTarget) {
                auto result = inputDeviceInstance->setTarget(slot->itemID, false, nullptr);
                if (!result.has_value())
                    continue;
                changedRouting = true;
            }

            inputDeviceInstance->setRecordingEnabled(track->itemID, false);
            inputDeviceInstance->setRecordingEnabled(slot->itemID, true);
            armedSlot = true;
        } else {
            const auto matchesSlotTarget = [&](auto targetID) { return targetID == slot->itemID; };
            const bool hadSlotTarget =
                std::ranges::any_of(inputDeviceInstance->getTargets(), matchesSlotTarget);

            if (hadSlotTarget) {
                inputDeviceInstance->setRecordingEnabled(slot->itemID, false);
                if (inputDeviceInstance->removeTarget(slot->itemID, nullptr))
                    changedRouting = true;
            }

            const auto matchesTrackTarget = [&](auto targetID) {
                return targetID == track->itemID;
            };
            const bool hasTrackTarget =
                std::ranges::any_of(inputDeviceInstance->getTargets(), matchesTrackTarget);
            if (hasTrackTarget)
                inputDeviceInstance->setRecordingEnabled(track->itemID, trackInfo->recordArmed);
        }
    }

    if (changedRouting && playbackContext->isPlaybackGraphAllocated())
        playbackContext->reallocate();

    syncTrackMidiPreviewConsumers();

    return enabled ? armedSlot : true;
}

void MidiInputRouter::setSurfaceOnlyMidiInputPort(const juce::String& midiDeviceIdOrName) {
    {
        juce::ScopedLock lock(surfaceOnlyMidiInputLock_);
        surfaceOnlyMidiInputPorts_.clear();
        if (midiDeviceIdOrName.isNotEmpty()) {
            surfaceOnlyMidiInputPorts_.addIfNotAlreadyThere(midiDeviceIdOrName);

            if (auto resolved = magda::midi::resolve(juce::MidiInput::getAvailableDevices(),
                                                     midiDeviceIdOrName)) {
                surfaceOnlyMidiInputPorts_.addIfNotAlreadyThere(resolved->identifier);
                surfaceOnlyMidiInputPorts_.addIfNotAlreadyThere(resolved->name);
            }
        }
    }

    removeSurfaceOnlyMidiInputTargets();
    updateMidiInputRouting();
}

void MidiInputRouter::clearSurfaceOnlyMidiInputPorts() {
    {
        juce::ScopedLock lock(surfaceOnlyMidiInputLock_);
        surfaceOnlyMidiInputPorts_.clear();
    }

    updateMidiInputRouting();
}

juce::String MidiInputRouter::getTrackMidiInput(TrackId trackId) const {
    auto* track = trackController_.getAudioTrack(trackId);
    if (!track)
        return {};

    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext)
        return {};

    const auto matchesTrackTarget = [&](auto targetID) { return targetID == track->itemID; };

    // Internal track routing takes precedence over hardware devices
    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        TrackId sourceTrackId = INVALID_TRACK_ID;
        if (getLiveTrackMidiInputDevice(trackController_, inputDeviceInstance, &sourceTrackId) &&
            std::ranges::any_of(inputDeviceInstance->getTargets(), matchesTrackTarget))
            return "track:" + juce::String(sourceTrackId);
    }

    juce::StringArray midiInputs;
    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        if (auto* midiDevice = getLiveMidiInputDevice(engine_, inputDeviceInstance)) {
            for (auto targetID : inputDeviceInstance->getTargets())
                if (matchesTrackTarget(targetID))
                    midiInputs.add(midiDevice->getName());
        }
    }

    if (midiInputs.isEmpty())
        return {};
    if (midiInputs.size() == 1)
        return midiInputs[0];

    return "all";
}

void MidiInputRouter::updateMidiInputRouting() {
    auto& tm = TrackManager::getInstance();

    // A track receives live MIDI input purely from its own input-monitor state
    // (In/Auto) or because it is record-armed. Selection does NOT gate this: a
    // monitored track keeps listening whether or not it is the selected track.
    // MultiOut child tracks forward their MIDI to the parent that hosts the
    // instrument, so their want is expressed against the parent id.
    std::unordered_set<TrackId> midiTargets;
    for (const auto& track : tm.getTracks()) {
        if (!track.takesExternalInput())
            continue;

        if (!track.receivesLiveMidiInput())
            continue;

        TrackId target = track.id;
        if (track.type == TrackType::MultiOut && track.hasParent())
            target = track.parentId;
        midiTargets.insert(target);
    }

    for (const auto& track : tm.getTracks()) {
        if (!track.takesExternalInput())
            continue;

        // Explicit internal routing is managed by setTrackMidiInput — don't
        // overlay it with "all" hardware inputs or tear it down here.
        if (track.midiInputDevice.startsWith("track:"))
            continue;

        bool shouldReceiveMidi = midiTargets.count(track.id) > 0;

        std::function<bool(const std::vector<ChainElement>&)> checkElements;
        checkElements = [&](const std::vector<ChainElement>& elements) -> bool {
            for (const auto& element : elements) {
                if (isDevice(element)) {
                    const auto& device = getDevice(element);
                    if (device.isInstrument)
                        return true;
                    if (device.pluginId.containsIgnoreCase(
                            daw::audio::MidiChordEnginePlugin::xmlTypeName))
                        return true;
                    for (const auto& mod : device.mods) {
                        if (mod.enabled && mod.triggerMode == LFOTriggerMode::MIDI)
                            return true;
                    }
                } else if (isRack(element)) {
                    const auto& rack = getRack(element);
                    for (const auto& mod : rack.mods) {
                        if (mod.enabled && mod.triggerMode == LFOTriggerMode::MIDI)
                            return true;
                    }
                    for (const auto& chain : rack.chains)
                        if (checkElements(chain.elements))
                            return true;
                }
            }
            return false;
        };

        bool needsMidi = checkElements(track.chain.fxChainElements);
        if (!needsMidi && !track.recordArmed)
            continue;

        juce::String currentMidi = getTrackMidiInput(track.id);
        bool currentlyRouted = currentMidi.isNotEmpty();

        if (shouldReceiveMidi && !currentlyRouted) {
            setTrackMidiInput(track.id, "all");
        } else if (!shouldReceiveMidi && currentlyRouted) {
            setTrackMidiInput(track.id, "");
        }
    }
}

void MidiInputRouter::resyncAllInputMonitors() {
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext)
        return;

    auto& tm = TrackManager::getInstance();
    std::vector<std::pair<te::InputDevice*, te::InputDevice::MonitorMode>> pendingModes;

    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        bool anyIn = false;
        bool anyAuto = false;

        for (auto targetID : inputDeviceInstance->getTargets()) {
            const auto matchesTarget = [&](const TrackInfo& trackInfo) {
                auto* track = trackController_.getAudioTrack(trackInfo.id);
                return track && targetID == track->itemID;
            };
            const auto& tracks = tm.getTracks();
            if (const auto found = std::ranges::find_if(tracks, matchesTarget);
                found != tracks.end()) {
                switch (found->inputMonitor) {
                    case InputMonitorMode::In:
                        anyIn = true;
                        break;
                    case InputMonitorMode::Auto:
                        anyAuto = true;
                        break;
                    case InputMonitorMode::Off:
                        break;
                }
            }
        }

        auto teMode = te::InputDevice::MonitorMode::off;
        if (anyIn)
            teMode = te::InputDevice::MonitorMode::on;
        else if (anyAuto)
            teMode = te::InputDevice::MonitorMode::automatic;

        // Hardware devices live in the DeviceManager; track-as-input devices
        // (audio or MIDI) are owned by the source track, so fall back to the
        // track mapping — otherwise "track:" inputs keep TE's default
        // "automatic" mode and only monitor while recording.
        te::InputDevice* inputDevice = getLiveInputDevice(engine_, inputDeviceInstance);
        if (!inputDevice)
            inputDevice = getLiveTrackWaveInputDevice(trackController_, inputDeviceInstance);
        if (!inputDevice)
            inputDevice = getLiveTrackMidiInputDevice(trackController_, inputDeviceInstance);
        if (inputDevice)
            pendingModes.emplace_back(inputDevice, teMode);
    }

    // Apply AFTER iterating: setMonitorMode() calls
    // TransportControl::restartAllTransports(), which can stop an active
    // recording and rebuild the playback context's input instances —
    // mutating the very container we're walking above. Device pointers are
    // stable (hardware devices belong to the DeviceManager, track devices
    // to their source AudioTrack), so applying from a snapshot is safe.
    for (auto& [inputDevice, teMode] : pendingModes)
        inputDevice->setMonitorMode(teMode);
}

void MidiInputRouter::onMidiDevicesAvailable() {
    DBG("AudioBridge::onMidiDevicesAvailable() - MIDI devices are now ready");

    auto& dm = engine_.getDeviceManager();
    auto midiDevices = dm.getMidiInDevices();
    DBG("  Available MIDI input devices: " << midiDevices.size());
    for (const auto& dev : midiDevices) {
        if (dev) {
            DBG("    - " << dev->getName() << " (enabled=" << (dev->isEnabled() ? "yes" : "no")
                         << ")");
        }
    }

    applyPendingRoutes();
}

void MidiInputRouter::applyPendingRoutes() {
    if (pendingMidiRoutes_.empty())
        return;

    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext)
        return;

    DBG("Applying " << pendingMidiRoutes_.size() << " pending MIDI routes");

    auto routes = std::move(pendingMidiRoutes_);
    pendingMidiRoutes_.clear();

    for (const auto& [trackId, midiDeviceId] : routes)
        setTrackMidiInput(trackId, midiDeviceId);
}

void MidiInputRouter::handlePlaybackContextTick() {
    applyPendingRoutes();

    auto* currentContext = edit_.getCurrentPlaybackContext();
    if (currentContext != lastPlaybackContext_) {
        lastPlaybackContext_ = currentContext;
        if (currentContext != nullptr)
            updateMidiInputRouting();
    }

    // Keep track-MIDI preview consumers registered/armed. Runs every tick:
    // input instances are recreated behind our back (restartAllTransports on
    // monitor-mode changes, device-list rebuilds) and arm state changes are
    // not routed through this class, so reconciliation is the robust model.
    syncTrackMidiPreviewConsumers();
}

}  // namespace magda
