// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "device_api.hpp"
#include "magda_api.hpp"
#include "remote_handlers.hpp"

namespace magda::remote::handlers {

namespace {

struct ParameterWrite {
    ChainNodePath path;
    int index = -1;
    float value = 0.0f;
};

/// One write checked by devices.setParameter's rules; a refusal's message starts with @p where.
std::optional<ParameterWrite> resolveParameterWrite(MagdaApi& api, const juce::var& write,
                                                    const juce::String& where,
                                                    HandlerResult& failure) {
    const auto refuse = [&](ErrorCode code, const juce::String& message) {
        failure = HandlerResult::fail(code, where + message);
        return std::optional<ParameterWrite>{};
    };
    const auto path = toChainNodePath(devicePathFromJson(write["devicePath"]));
    if (!path)
        return refuse(ErrorCode::ValidationFailed, "devicePath does not resolve");
    const auto* device = api.devices().getDevice(*path);
    if (device == nullptr)
        return refuse(ErrorCode::NotFound, "no device at devicePath");

    const auto parameterIndex = static_cast<int>(write["parameterIndex"]);
    const auto parameters = makeDeviceParameterDtos(*device, *path);
    const auto named = std::ranges::find(parameters, parameterIndex, &DeviceParameterDto::index);
    if (named == parameters.end())
        return refuse(ErrorCode::NotFound,
                      "parameter " + juce::String(parameterIndex) + " not found");

    // DeviceApi::setDeviceParameter enforces the user's per-parameter opt-in
    // for every facade consumer; this pre-check only exists to classify the
    // refusal as PermissionDenied with a useful message, which the facade's
    // bare bool cannot convey (#2296).
    if (!named->aiAgentEnabled)
        return refuse(ErrorCode::PermissionDenied,
                      "parameter " + juce::String(parameterIndex) +
                          " is not enabled for AI agent control; enable it under "
                          "Configure Parameters");

    // Reject rather than clamp, for the same reason the facade does: a clamped
    // write reports success while setting a value the caller did not ask for.
    const auto value = static_cast<double>(write["value"]);
    if (!std::isfinite(value) || value < named->minValue || value > named->maxValue)
        return refuse(ErrorCode::ValidationFailed,
                      "value " + juce::String(value) + " is outside [" +
                          juce::String(named->minValue) + ", " + juce::String(named->maxValue) +
                          "] " + named->unit);
    return ParameterWrite{*path, parameterIndex, static_cast<float>(value)};
}

/// The parameter as the model now holds it, through the projection listParameters uses.
std::optional<juce::var> writtenParameter(MagdaApi& api, const ParameterWrite& write) {
    if (const auto* updated = api.devices().getDevice(write.path))
        for (const auto& parameter : makeDeviceParameterDtos(*updated, write.path))
            if (parameter.index == write.index)
                return toJson(parameter);
    return std::nullopt;
}

}  // namespace

HandlerResult devicesSetParameter(MagdaApi& api, const juce::var& input, const RequestContext&) {
    HandlerResult failure = HandlerResult::fail(ErrorCode::InternalError, "unreachable");
    const auto write = resolveParameterWrite(api, input, {}, failure);
    if (!write)
        return failure;
    if (!api.devices().setDeviceParameter(write->path, write->index, write->value))
        return HandlerResult::fail(ErrorCode::Conflict, "parameter write was rejected");
    if (auto parameter = writtenParameter(api, *write))
        return HandlerResult::ok(std::move(*parameter));
    return HandlerResult::fail(ErrorCode::InternalError, "parameter vanished after write");
}

HandlerResult devicesSetParameters(MagdaApi& api, const juce::var& input, const RequestContext&) {
    HandlerResult failure = HandlerResult::fail(ErrorCode::InternalError, "unreachable");
    std::vector<ParameterWrite> writes;
    const auto& items = *input["writes"].getArray();
    for (int i = 0; i < items.size(); ++i) {
        auto write = resolveParameterWrite(api, items.getReference(i),
                                           "writes[" + juce::String(i) + "]: ", failure);
        if (!write)
            return failure;
        writes.push_back(*write);
    }

    // Checked whole above, so nothing is written unless every write can land (#3347).
    for (const auto& write : writes)
        if (!api.devices().setDeviceParameter(write.path, write.index, write.value))
            return HandlerResult::fail(ErrorCode::Conflict, "parameter write was rejected");
    std::vector<juce::var> results;
    for (const auto& write : writes) {
        auto parameter = writtenParameter(api, write);
        if (!parameter)
            return HandlerResult::fail(ErrorCode::InternalError, "parameter vanished after write");
        results.push_back(std::move(*parameter));
    }
    juce::Array<juce::var> written;
    for (auto& result : results)
        written.add(std::move(result));
    return HandlerResult::ok(juce::var(written));
}

}  // namespace magda::remote::handlers
