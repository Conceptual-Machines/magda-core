#include "ExternalPluginState.hpp"

#include <algorithm>

#include "../Vst3Preset.hpp"
#include "core/ParameterUtils.hpp"

namespace magda {

namespace {

/// The slots the host puts in front of a plugin's own: dry, then wet.
constexpr int kWrapperParameterCount = 2;

/// The model's record of the parameter at @p index, or none.
///
/// Both buckets, because MAGDA splits the incumbent's one list in two: the
/// plugin's parameters and the wrapper pair it never declared. Both carry the
/// index a project addresses them by.
const ParameterInfo* modelParameterAt(const DeviceInfo& device, int index) {
    for (const auto* bucket : {&device.parameters, &device.wrapperParameters})
        for (const auto& info : *bucket)
            if (info.paramIndex == index)
                return &info;

    return nullptr;
}

/// One visit to the VST3 extension, in whichever direction was asked for. The
/// extension is the only way to ask an instance whether it is a VST3 at all:
/// visitVST3Client() is called for one and nothing is called for anything else,
/// so `visited` is the answer to both questions at once.
struct Vst3PresetVisitor final : juce::ExtensionsVisitor {
    juce::MemoryBlock preset;
    bool writing = false;
    bool visited = false;
    bool accepted = false;

    void visitVST3Client(const VST3Client& client) override {
        visited = true;

        if (writing)
            accepted = client.setPreset(preset);
        else
            preset = client.getPreset();
    }
};

/// The saved chunk, decoded. Empty for a device that saved none and for a
/// string that is not base64, which is what a project truncated by a failed
/// write looks like.
juce::MemoryBlock decodeSavedChunk(const juce::String& savedState) {
    juce::MemoryBlock chunk;

    if (savedState.isEmpty())
        return chunk;

    if (!chunk.fromBase64Encoding(savedState))
        chunk.reset();

    return chunk;
}

/// The saved .vstpreset, decoded. Standard base64 rather than
/// juce::MemoryBlock's own, because this one crosses between hosts and the
/// DAWproject writer that produced it used the standard one.
juce::MemoryBlock decodeSavedPreset(const juce::String& savedPreset) {
    juce::MemoryOutputStream decoded;

    if (savedPreset.isEmpty() || !juce::Base64::convertFromBase64(decoded, savedPreset))
        return {};

    return decoded.getMemoryBlock();
}

}  // namespace

std::vector<juce::AudioProcessorParameter*> hostParameterOrder(
    const juce::AudioPluginInstance& instance) {
    std::vector<juce::AudioProcessorParameter*> order;

    // The wrapper pair, which nothing on the plugin stands behind.
    order.resize(kWrapperParameterCount, nullptr);

    for (auto* parameter : instance.getParameters())
        if (parameter != nullptr && parameter->isAutomatable())
            order.push_back(parameter);

    return order;
}

SavedStateOutcome applySavedPluginState(juce::AudioPluginInstance& instance,
                                        const DeviceInfo& device) {
    const auto order = hostParameterOrder(instance);

    // The array first. A parameter the model does not describe keeps whatever
    // the plugin was built with: the project has nothing to say about it, which
    // is what a plugin that has gained a parameter since the project was saved
    // looks like.
    for (int index = 0; index < static_cast<int>(order.size()); ++index) {
        auto* parameter = order[static_cast<std::size_t>(index)];
        if (parameter == nullptr)
            continue;

        const auto* info = modelParameterAt(device, index);
        if (info == nullptr)
            continue;

        parameter->setValue(
            std::clamp(ParameterUtils::realToNormalized(info->currentValue, *info), 0.0f, 1.0f));
    }

    // The portable preset is asked first, because a project only carries one
    // until the load that consumes it: an import has a .vstpreset and no chunk,
    // and everything saved afterwards has a chunk and no preset. A plugin that
    // refuses it -- a VST3 that will not take that patch, or a format with no
    // preset call at all -- falls through to the chunk rather than being left on
    // the bare array, which is the only place the project's own record is.
    if (const auto preset = decodeSavedPreset(device.vst3Preset); preset.getSize() > 0) {
        if (writeVst3Preset(instance, preset))
            return SavedStateOutcome::RestoredFromPreset;
    }

    const auto chunk = decodeSavedChunk(device.pluginState);
    if (chunk.getSize() == 0)
        return SavedStateOutcome::Baseline;

    try {
        instance.setStateInformation(chunk.getData(), static_cast<int>(chunk.getSize()));
    } catch (...) {
        // The host survives, which is all a catch-all can promise. What the
        // plugin holds now is whatever it had managed to do before it threw:
        // half a preset, a program it switched, a sample it swapped. Writing
        // the parameter array again would put the parameters back and none of
        // the rest, so this says so rather than pretending otherwise.
        return SavedStateOutcome::Failed;
    }

    return SavedStateOutcome::Restored;
}

std::vector<RestoredParameter> snapshotHostParameters(const juce::AudioPluginInstance& instance) {
    const auto order = hostParameterOrder(instance);

    std::vector<RestoredParameter> restored;
    restored.reserve(order.size());

    for (int index = 0; index < static_cast<int>(order.size()); ++index)
        if (auto* parameter = order[static_cast<std::size_t>(index)]; parameter != nullptr)
            restored.push_back({.paramIndex = index, .value = parameter->getValue()});

    return restored;
}

void applyRestoredParameters(DeviceInfo& device, const std::vector<RestoredParameter>& restored) {
    for (const auto& parameter : restored)
        for (auto* bucket : {&device.parameters, &device.wrapperParameters})
            for (auto& info : *bucket)
                if (info.paramIndex == parameter.paramIndex)
                    info.currentValue = ParameterUtils::normalizedToReal(
                        parameter.value, ParameterUtils::domainOf(info));
}

juce::MemoryBlock readVst3Preset(const juce::AudioPluginInstance& instance) {
    Vst3PresetVisitor visitor;

    try {
        instance.getExtensions(visitor);
    } catch (...) {
        return {};
    }

    return visitor.preset;
}

bool writeVst3Preset(juce::AudioPluginInstance& instance, const juce::MemoryBlock& preset) {
    if (preset.getSize() == 0)
        return false;

    Vst3PresetVisitor visitor;
    visitor.writing = true;
    visitor.preset = preset;

    try {
        instance.getExtensions(visitor);
    } catch (...) {
        return false;
    }

    return visitor.accepted;
}

void captureVst3Records(const juce::AudioPluginInstance& instance, DeviceInfo& device) {
    const auto preset = readVst3Preset(instance);
    if (preset.getSize() == 0)
        return;

    if (device.vst3ClassId.isEmpty())
        device.vst3ClassId = vst3::classIdFromPreset(preset);

    device.vst3Preset = juce::Base64::toBase64(preset.getData(), preset.getSize());
}

bool captureSavedPluginState(juce::AudioPluginInstance& instance, DeviceInfo& device) {
    juce::MemoryBlock chunk;

    // Suspended across the read, the way the fork suspends it
    // (ExternalPlugin::flushPluginStateToValueTree). Restored either way: a
    // plugin left suspended by its own throw would render silence for the rest
    // of the session.
    instance.suspendProcessing(true);

    bool described = true;
    try {
        instance.getStateInformation(chunk);
    } catch (...) {
        described = false;
    }

    instance.suspendProcessing(false);

    if (!described)
        return false;

    // Absent rather than empty for a plugin with nothing to say, which is what
    // the fork writes for one: it removes the property rather than storing a
    // zero-length chunk, and a project that stored one would come back through
    // decodeSavedChunk() as a baseline anyway.
    device.pluginState = chunk.getSize() > 0 ? chunk.toBase64Encoding() : juce::String();

    applyRestoredParameters(device, snapshotHostParameters(instance));
    captureVst3Records(instance, device);

    return true;
}

}  // namespace magda
