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

LocalDeviceControlPlane::LocalDeviceControlPlane(DeviceLookup devices)
    : devices_(std::move(devices)) {}

void LocalDeviceControlPlane::captureState(magda::engine::DeviceKey key,
                                           CaptureCallback completed) {
    if (!completed)
        return;

    if (!devices_) {
        completed({.snapshot = std::nullopt,
                   .failure = "this control plane was given no way to find a device"});
        return;
    }

    auto* device = devices_(key);
    if (device == nullptr) {
        // Named rather than reported as an empty state. A key with nothing
        // bound to it is a slot whose plugin has not arrived or has gone, and a
        // caller told "no state" would write that absence into the project.
        completed(
            {.snapshot = std::nullopt, .failure = "no plugin is bound for " + describeKey(key)});
        return;
    }

    auto snapshot = device->captureState();
    if (!snapshot.has_value()) {
        completed(
            {.snapshot = std::nullopt,
             .failure = "the plugin bound for " + describeKey(key) + " could not describe itself"});
        return;
    }

    // Called before returning, because the plugin is right here. Through the
    // callback all the same: a caller that only worked when the answer arrived
    // synchronously would be a caller that only works in this process.
    completed({.snapshot = std::move(snapshot), .failure = {}});
}

bool commitCapturedState(const AssignmentRequest& request,
                         const magda::ExternalPluginSnapshot& snapshot, magda::DeviceInfo& device) {
    // The same question the other direction asks of a plugin that has finished
    // loading, and the same answer: a snapshot is only worth writing down onto
    // the assignment it was read from. A key that is live again under a
    // different assignment is not that, and neither is a runtime that has gone.
    if (!request.isStillWanted())
        return false;

    magda::applyCapturedPluginState(device, snapshot);
    return true;
}

}  // namespace magda::daw::audio::engine_adapter
