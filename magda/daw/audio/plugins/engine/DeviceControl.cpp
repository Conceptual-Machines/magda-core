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
    // The jassert above is this accessor's contract; it compiles out in
    // release, where a caller that skipped ok() has already broken it.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return *snapshot_;
}

DeviceControlPlane::DeviceControlPlane(std::shared_ptr<ControlExecutor> executor)
    : executor_(std::move(executor)) {
    // A plane with nowhere to run its work would answer nothing and say nothing
    // about why, which is the one failure this whole file is arranged against.
    jassert(executor_ != nullptr);
}

LocalDeviceControlPlane::LocalDeviceControlPlane(std::shared_ptr<ControlExecutor> executor,
                                                 std::weak_ptr<const DeviceRegistry> devices)
    : DeviceControlPlane(std::move(executor)), devices_(std::move(devices)) {}

bool LocalDeviceControlPlane::captureState(magda::engine::DeviceKey key,
                                           CaptureCallback completed) {
    if (!completed || executor() == nullptr)
        return false;

    // Everything past here runs on the executor: the lookup, the plugin's own
    // state read, and the answer. Nothing checks which thread asked, because
    // asking is allowed from any of them -- what is not allowed is two of these
    // being inside one plugin at once, and one serial executor is what makes
    // that impossible for every control operation rather than for this one.
    //
    // Nothing about the plane is captured. The work outlives the call that
    // queued it, so it carries what it needs by value: the weak registry, which
    // is the devices and their owner in one reference rather than a token
    // standing beside them.
    return executor()->run([devices = devices_, key, completed](ExecutionState state) mutable {
        if (state == ExecutionState::Cancelled) {
            // Still on the executor, which is what a cancellation is worth: the
            // caller is told where it was expecting to be told, and the devices
            // are not reached for on the way.
            completed(CaptureOutcome::failed("the control plane closed before this capture ran"));
            return;
        }

        // Held for the length of the operation rather than checked at the start
        // of it, and it is the registry itself rather than a token beside it:
        // what keeps a device alive is the thing that owns it, so locking that
        // is what makes the pointer below safe to use until this returns.
        const auto registry = devices.lock();
        if (!registry) {
            completed(CaptureOutcome::failed("the runtime that owned " + describeKey(key) +
                                             " is gone, so nothing was read"));
            return;
        }

        // The lease, held for the rest of this operation. What it is worth is
        // that the device cannot go while the capture is reading it: not when
        // the chain it sits in is edited, and not when whatever owns it lets
        // go. The registry being alive was never enough to promise that.
        const auto device = registry->find(key);
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
