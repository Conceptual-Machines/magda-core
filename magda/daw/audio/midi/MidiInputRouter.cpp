#include "midi/MidiInputRouter.hpp"

#include <functional>

#include "../../core/ClipManager.hpp"
#include "../../core/RackInfo.hpp"
#include "../../core/TrackManager.hpp"
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

}  // namespace

MidiInputRouter::MidiInputRouter(te::Engine& engine, te::Edit& edit,
                                 TrackController& trackController)
    : engine_(engine), edit_(edit), trackController_(trackController) {}

te::VirtualMidiInputDevice* MidiInputRouter::getQwertyMidiDevice() {
    if (!qwertyMidiDevice_) {
        // Check if it already exists (persisted from a previous session).
        // Only accept actual VirtualMidiInputDevice instances — a physical
        // device with the same name would break the cast and leave the
        // feature silently disabled.
        for (auto& dev : engine_.getDeviceManager().getMidiInDevices()) {
            if (dev->getName() == "QWERTY Keyboard" &&
                dynamic_cast<te::VirtualMidiInputDevice*>(dev.get())) {
                qwertyMidiDevice_ = dev;
                break;
            }
        }

        if (!qwertyMidiDevice_) {
            auto result = engine_.getDeviceManager().createVirtualMidiDevice("QWERTY Keyboard");
            if (result.wasOk()) {
                for (auto& dev : engine_.getDeviceManager().getMidiInDevices()) {
                    if (dev->getName() == "QWERTY Keyboard" &&
                        dynamic_cast<te::VirtualMidiInputDevice*>(dev.get())) {
                        qwertyMidiDevice_ = dev;
                        break;
                    }
                }
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

    for (const auto& key : keys) {
        if (magda::midi::matches(key, liveIdentifier, liveName))
            return true;
    }

    return false;
}

void MidiInputRouter::removeSurfaceOnlyMidiInputTargets() {
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext)
        return;

    bool removedAnyRouting = false;
    auto& tm = TrackManager::getInstance();

    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        if (auto* midiDevice = dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
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

void MidiInputRouter::setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId) {
    auto* track = trackController_.getAudioTrack(trackId);
    if (!track)
        return;

    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext) {
        pendingMidiRoutes_.push_back({trackId, midiDeviceId});
        return;
    }

    if (midiDeviceId.isEmpty()) {
        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            if (dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) [[maybe_unused]]
                auto result = inputDeviceInstance->removeTarget(track->itemID, nullptr);
        }
    } else if (midiDeviceId == "all") {
        bool addedAnyRouting = false;
        bool removedAnyRouting = false;

        auto teMonitorMode = te::InputDevice::MonitorMode::on;
        if (auto* trackInfo = TrackManager::getInstance().getTrack(trackId))
            teMonitorMode = toTeMonitorMode(trackInfo->inputMonitor);

        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            if (auto* midiDevice =
                    dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
                if (midiDevice->getName() == "All MIDI Ins")
                    continue;

                if (isSurfaceOnlyMidiInput(midiDevice->getDeviceID(), midiDevice->getName())) {
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
            for (const auto& d : juceDevices) {
                if (d.identifier == midiDeviceId) {
                    deviceName = d.name;
                    break;
                }
            }

            if (deviceName.isNotEmpty()) {
                for (const auto& device : dm.getMidiInDevices()) {
                    if (device && device->getName() == deviceName) {
                        midiDevice = device.get();
                        break;
                    }
                }
            }
        }

        if (midiDevice) {
            if (isSurfaceOnlyMidiInput(midiDevice->getDeviceID(), midiDevice->getName())) {
                bool removedAnyRouting = false;
                for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
                    if (&inputDeviceInstance->owner == midiDevice) {
                        if (inputDeviceInstance->removeTarget(track->itemID, nullptr))
                            removedAnyRouting = true;
                        break;
                    }
                }
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

            for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
                if (&inputDeviceInstance->owner == midiDevice) {
                    auto result = inputDeviceInstance->setTarget(track->itemID, true, nullptr);
                    if (result.has_value()) {
                        (*result)->recordEnabled = false;
                        addedRouting = true;
                    }
                    break;
                }
            }
        }

        if (addedRouting && playbackContext->isPlaybackGraphAllocated())
            playbackContext->reallocate();
    }
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
    updateForSelection();
}

void MidiInputRouter::clearSurfaceOnlyMidiInputPorts() {
    {
        juce::ScopedLock lock(surfaceOnlyMidiInputLock_);
        surfaceOnlyMidiInputPorts_.clear();
    }

    updateForSelection();
}

juce::String MidiInputRouter::getTrackMidiInput(TrackId trackId) const {
    auto* track = trackController_.getAudioTrack(trackId);
    if (!track)
        return {};

    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext)
        return {};

    juce::StringArray midiInputs;
    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        if (dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
            auto targets = inputDeviceInstance->getTargets();
            for (auto targetID : targets) {
                if (targetID == track->itemID)
                    midiInputs.add(inputDeviceInstance->owner.getName());
            }
        }
    }

    if (midiInputs.isEmpty())
        return {};
    if (midiInputs.size() == 1)
        return midiInputs[0];

    return "all";
}

void MidiInputRouter::updateForSelection() {
    auto& tm = TrackManager::getInstance();
    auto selectedTrackId = tm.getSelectedTrack();

    TrackId midiTrackId = selectedTrackId;
    if (midiTrackId != INVALID_TRACK_ID) {
        const auto* selectedTrack = tm.getTrack(midiTrackId);
        if (selectedTrack && selectedTrack->type == TrackType::MultiOut &&
            selectedTrack->hasParent()) {
            midiTrackId = selectedTrack->parentId;
        }
    }
    if (midiTrackId == INVALID_TRACK_ID) {
        auto selectedClipId = ClipManager::getInstance().getSelectedClip();
        if (selectedClipId != INVALID_CLIP_ID) {
            if (auto* clip = ClipManager::getInstance().getClip(selectedClipId))
                midiTrackId = clip->trackId;
        }
    }

    for (const auto& track : tm.getTracks()) {
        if (track.type == TrackType::Aux)
            continue;

        bool shouldReceiveMidi = (track.id == midiTrackId) || track.recordArmed;

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

        bool needsMidi = checkElements(track.chainElements);
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

    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        bool anyIn = false;
        bool anyAuto = false;

        for (auto targetID : inputDeviceInstance->getTargets()) {
            for (const auto& trackInfo : tm.getTracks()) {
                auto* track = trackController_.getAudioTrack(trackInfo.id);
                if (!track || targetID != track->itemID)
                    continue;

                switch (trackInfo.inputMonitor) {
                    case InputMonitorMode::In:
                        anyIn = true;
                        break;
                    case InputMonitorMode::Auto:
                        anyAuto = true;
                        break;
                    case InputMonitorMode::Off:
                        break;
                }
                break;
            }
        }

        auto teMode = te::InputDevice::MonitorMode::off;
        if (anyIn)
            teMode = te::InputDevice::MonitorMode::on;
        else if (anyAuto)
            teMode = te::InputDevice::MonitorMode::automatic;

        inputDeviceInstance->owner.setMonitorMode(teMode);
    }
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
            updateForSelection();
    }
}

}  // namespace magda
