#include "DeviceControl.hpp"

#include <utility>

#include "EngineExternalDevice.hpp"

namespace magda::daw::audio::engine_adapter {

namespace {

/// Which device a key names, said the way a person reads it.
juce::String describeKey(magda::engine::DeviceKey key) {
    return "device " + juce::String(static_cast<int>(key.deviceId)) + " in section " +
           juce::String(static_cast<int>(key.segment));
}

}  // namespace

CaptureOutcome CaptureOutcome::taken(magda::ExternalPluginSnapshot snapshot) {
    CaptureOutcome outcome;
    outcome.snapshot_ = std::move(snapshot);
    return outcome;
}

CaptureOutcome CaptureOutcome::failed(juce::String reason) {
    CaptureOutcome outcome;

    // A failure always says something. One that did not would reach a person as
    // a save that did not happen and no reason it did not, which is the same
    // thing as saying nothing at all.
    outcome.failure_ = reason.isNotEmpty()
                           ? std::move(reason)
                           : juce::String("the capture failed for no stated reason");
    return outcome;
}

const magda::ExternalPluginSnapshot& CaptureOutcome::snapshot() const {
    jassert(ok());
    return *snapshot_;
}

LocalDeviceControlPlane::LocalDeviceControlPlane(DeviceLookup devices)
    : devices_(std::move(devices)) {}

void LocalDeviceControlPlane::captureState(magda::engine::DeviceKey key,
                                           CaptureCallback completed) {
    if (!completed)
        return;

    // The precondition, enforced rather than documented. Everything below this
    // line reaches into a plugin: the lookup finds the device, the device
    // suspends the instance and asks it to describe itself, and none of that is
    // a worker thread's to do. Refused rather than marshalled, because a save
    // asked for from the wrong thread is a caller to fix and not a call to
    // rescue -- and because a remote implementation would answer the same way
    // rather than quietly acquiring a thread to hop from.
    //
    // Asked as "is there a message thread, and is this it" rather than as "is
    // this the message thread", because those differ where the engine is
    // supposed to work: a headless render has no message manager at all, so
    // there is no thread to be off, and a check that refused there would refuse
    // every offline host on the grounds that it is not an application.
    if (auto* messages = juce::MessageManager::getInstanceWithoutCreating();
        messages != nullptr && !messages->isThisTheMessageThread()) {
        JUCE_ASSERT_MESSAGE_THREAD
        completed(CaptureOutcome::failed("a capture was asked for off the message thread"));
        return;
    }

    if (!devices_) {
        completed(CaptureOutcome::failed("this control plane was given no way to find a device"));
        return;
    }

    auto* device = devices_(key);
    if (device == nullptr) {
        // Named rather than reported as an empty state. A key with nothing
        // bound to it is a slot whose plugin has not arrived or has gone, and a
        // caller told "no state" would write that absence into the project.
        completed(CaptureOutcome::failed("no plugin is bound for " + describeKey(key)));
        return;
    }

    auto snapshot = device->captureState();
    if (!snapshot.has_value()) {
        completed(CaptureOutcome::failed("the plugin bound for " + describeKey(key) +
                                         " could not describe itself"));
        return;
    }

    // Called before returning, because the plugin is right here. Through the
    // callback all the same: a caller that only worked when the answer arrived
    // synchronously would be a caller that only works in this process.
    completed(CaptureOutcome::taken(std::move(*snapshot)));
}

bool commitCapturedState(const AssignmentRequest& request,
                         const magda::ExternalPluginSnapshot& snapshot,
                         const MutableDeviceLookup& device) {
    // The same question the other direction asks of a plugin that has finished
    // loading, and the same answer: a snapshot is only worth writing down onto
    // the assignment it was read from. A key that is live again under a
    // different assignment is not that, and neither is a runtime that has gone.
    if (!request.isStillWanted())
        return false;

    if (!device)
        return false;

    // Resolved from the token's own key, so the thing that was validated and
    // the thing that is written are the same device by construction.
    auto* target = device(request.key);
    if (target == nullptr)
        return false;

    magda::applyCapturedPluginState(*target, snapshot);
    return true;
}

}  // namespace magda::daw::audio::engine_adapter
