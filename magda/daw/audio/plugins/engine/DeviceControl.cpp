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

DeviceControlPlane::DeviceControlPlane(std::shared_ptr<ControlExecutor> executor)
    : executor_(std::move(executor)) {
    // A plane with nowhere to run its work would answer nothing and say nothing
    // about why, which is the one failure this whole file is arranged against.
    jassert(executor_ != nullptr);
}

LocalDeviceControlPlane::LocalDeviceControlPlane(std::shared_ptr<ControlExecutor> executor,
                                                 DeviceLookup devices)
    : DeviceControlPlane(std::move(executor)), devices_(std::move(devices)) {}

void LocalDeviceControlPlane::captureState(magda::engine::DeviceKey key,
                                           CaptureCallback completed) {
    if (!completed)
        return;

    if (executor() == nullptr) {
        completed(CaptureOutcome::failed("this control plane has no executor to run on"));
        return;
    }

    // Everything past here runs on the executor: the lookup, the plugin's own
    // state read, and the answer. Nothing checks which thread asked, because
    // asking is allowed from any of them -- what is not allowed is two of these
    // being inside one plugin at once, and one serial executor is what makes
    // that impossible for every control operation rather than for this one.
    //
    // The lookup is copied rather than reached for through `this`: work outlives
    // the call that queued it, and a plane destroyed in between would otherwise
    // be read from a thread that is still running.
    executor()->run([devices = devices_, key, completed = std::move(completed)]() mutable {
        if (!devices) {
            completed(
                CaptureOutcome::failed("this control plane was given no way to find a device"));
            return;
        }

        auto* device = devices(key);
        if (device == nullptr) {
            // Named rather than reported as an empty state. A key with nothing
            // bound to it is a slot whose plugin has not arrived or has gone,
            // and a caller told "no state" would write that absence into the
            // project.
            completed(CaptureOutcome::failed("no plugin is bound for " + describeKey(key)));
            return;
        }

        auto snapshot = device->captureState();
        if (!snapshot.has_value()) {
            completed(CaptureOutcome::failed("the plugin bound for " + describeKey(key) +
                                             " could not describe itself"));
            return;
        }

        completed(CaptureOutcome::taken(std::move(*snapshot)));
    });
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
