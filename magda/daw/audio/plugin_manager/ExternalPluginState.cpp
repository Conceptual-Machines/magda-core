#include "ExternalPluginState.hpp"

#include <algorithm>
#include <map>

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

/// Write @p read into @p device's portable records. See captureVst3Records().
void applyVst3Records(DeviceInfo& device, const Vst3PresetRead& read) {
    if (!read.isVst3)
        return;

    if (read.preset.getSize() == 0) {
        device.vst3Preset = {};
        return;
    }

    if (device.vst3ClassId.isEmpty())
        device.vst3ClassId = vst3::classIdFromPreset(read.preset);

    device.vst3Preset = juce::Base64::toBase64(read.preset.getData(), read.preset.getSize());
}

/// The length the fork asks a plugin for its parameter names at.
constexpr int kParameterNameLength = 1024;

/// A record over [0, 1], which is the only shape an external plugin's
/// parameters take: the fork wraps every one of them in that range whatever the
/// plugin's own units are.
ParameterInfo normalisedParameter(int index, const juce::String& name) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = name;
    info.minValue = 0.0f;
    info.maxValue = 1.0f;
    info.teMinValue = 0.0f;
    info.teMaxValue = 1.0f;
    return info;
}

/// The fork's name for @p parameter, de-duplication included: a repeated name
/// gains a " (2)", and an unnamed one is numbered by its position in the
/// plugin's own array. @p used counts the names seen before this one.
juce::String hostParameterName(const juce::AudioProcessorParameter& parameter,
                               std::map<juce::String, int>& used) {
    const auto declared = parameter.getName(kParameterNameLength);
    const auto seen = ++used[declared.isEmpty() ? juce::String("Unnamed") : declared];

    if (declared.isEmpty())
        return "Unnamed " + juce::String(parameter.getParameterIndex() + 1);

    return seen > 1 ? declared + " (" + juce::String(seen) + ")" : declared;
}

/// The fork's id for @p parameter: the plugin's own where it declares one, and
/// its index otherwise.
juce::String hostParameterId(const juce::AudioProcessorParameter& parameter) {
    if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(&parameter))
        return withId->paramID;

    return juce::String(parameter.getParameterIndex());
}

/// One of the wrapper pair, at the value @p device holds for it.
ParameterInfo wrapperParameter(const DeviceInfo& device, int index, const juce::String& id,
                               const juce::String& name, WrapperRole role, float defaultValue) {
    auto info = normalisedParameter(index, name);
    info.stableId = id;
    info.wrapperRole = role;
    info.defaultValue = defaultValue;

    const auto* saved = modelParameterAt(device, index);
    info.currentValue = saved != nullptr ? saved->currentValue : defaultValue;
    return info;
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

HostParameters describeHostParameters(const juce::AudioPluginInstance& instance,
                                      const DeviceInfo& device) {
    HostParameters described;

    described.wrapperParameters = {
        wrapperParameter(device, 0, "dry level", "Dry Level", WrapperRole::DryGain, 0.0f),
        wrapperParameter(device, 1, "wet level", "Wet Level", WrapperRole::WetGain, 1.0f)};

    const auto order = hostParameterOrder(instance);
    std::map<juce::String, int> used;

    for (int index = kWrapperParameterCount; index < static_cast<int>(order.size()); ++index) {
        const auto* parameter = order[static_cast<std::size_t>(index)];
        if (parameter == nullptr)
            continue;

        auto info = normalisedParameter(index, hostParameterName(*parameter, used));
        info.stableId = hostParameterId(*parameter);
        info.defaultValue = parameter->getDefaultValue();
        info.currentValue = parameter->getValue();

        described.parameters.push_back(std::move(info));
    }

    return described;
}

/** @brief Suspends a plugin's processing for the lifetime of this object. */
class ScopedSuspend {
  public:
    explicit ScopedSuspend(juce::AudioPluginInstance& instance) : instance_(instance) {
        instance_.suspendProcessing(true);
    }

    ~ScopedSuspend() {
        instance_.suspendProcessing(false);
    }

    ScopedSuspend(const ScopedSuspend&) = delete;
    ScopedSuspend& operator=(const ScopedSuspend&) = delete;
    ScopedSuspend(ScopedSuspend&&) = delete;
    ScopedSuspend& operator=(ScopedSuspend&&) = delete;

  private:
    juce::AudioPluginInstance& instance_;
};

SavedStateOutcome applySavedPluginState(juce::AudioPluginInstance& instance,
                                        const DeviceInfo& device) {
    // Stops a render block reaching the plugin while its state is half
    // written.
    const ScopedSuspend held(instance);

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

        // Through the pair that knows which domain the model stores a value in,
        // rather than assuming the display one. They differ once a plugin has a
        // configured display range: the model keeps the plugin's own normalised
        // number and the range is what the UI draws it against (#2601).
        parameter->setValue(std::clamp(
            ParameterUtils::modelToNormalizedValue(ParameterModelValue{info->currentValue}, *info)
                .value,
            0.0f, 1.0f));
    }

    // The portable preset is asked first, because a project only carries one
    // until the load that consumes it: an import has a .vstpreset and no chunk,
    // and everything saved afterwards has a chunk and no preset. A plugin that
    // refuses it -- a VST3 that will not take that patch, or a format with no
    // preset call at all -- falls through to the chunk rather than being left on
    // the bare array, which is the only place the project's own record is.
    bool presetRefused = false;

    if (const auto preset = decodeSavedPreset(device.vst3Preset); preset.getSize() > 0) {
        switch (writeVst3Preset(instance, preset)) {
            case Vst3PresetOutcome::Applied:
                return SavedStateOutcome::RestoredFromPreset;
            case Vst3PresetOutcome::Refused:
                presetRefused = true;
                break;
            case Vst3PresetOutcome::NotVst3:
                break;
        }
    }

    const auto chunk = decodeSavedChunk(device.pluginState);
    if (chunk.getSize() == 0) {
        // A refusal with nothing behind it. Steinberg's loader restores the
        // component's state before the controller's and returns false if the
        // second fails after the first, so the plugin may be holding half the
        // imported patch; the parameter array written above describes what the
        // project believed and not what the instance now is. There is nothing
        // left to make it true, so this is the same answer the chunk path gives
        // for the same situation rather than a quieter one.
        if (presetRefused)
            return SavedStateOutcome::Failed;

        return SavedStateOutcome::Baseline;
    }

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
                    // The inverse of what applySavedPluginState() wrote, and
                    // the same pair the plan, the UI and automation read
                    // through: converting to the display range unconditionally
                    // would put a real value in a field everything else reads
                    // as normalised (#2601).
                    info.currentValue =
                        ParameterUtils::normalizedToModelValue(
                            ParameterNormalizedValue::clamped(parameter.value), info)
                            .value;
}

Vst3PresetRead readVst3Preset(const juce::AudioPluginInstance& instance) {
    Vst3PresetVisitor visitor;

    // The visit is what identifies the format, and it is recorded before the
    // plugin is asked anything: a VST3 whose getPreset() throws has still been
    // identified as one, and the caller's answer for it is not the answer for a
    // plugin that was never visited.
    try {
        instance.getExtensions(visitor);
    } catch (...) {
        return {.preset = {}, .isVst3 = visitor.visited};
    }

    return {.preset = std::move(visitor.preset), .isVst3 = visitor.visited};
}

Vst3PresetOutcome writeVst3Preset(juce::AudioPluginInstance& instance,
                                  const juce::MemoryBlock& preset) {
    if (preset.getSize() == 0)
        return Vst3PresetOutcome::NotVst3;

    Vst3PresetVisitor visitor;
    visitor.writing = true;
    visitor.preset = preset;

    try {
        instance.getExtensions(visitor);
    } catch (...) {
        // Visited and throwing is a refusal rather than a no-op, and for the
        // same reason a refusal is: whatever the loader had done before it threw
        // is still done.
        return visitor.visited ? Vst3PresetOutcome::Refused : Vst3PresetOutcome::NotVst3;
    }

    if (!visitor.visited)
        return Vst3PresetOutcome::NotVst3;

    return visitor.accepted ? Vst3PresetOutcome::Applied : Vst3PresetOutcome::Refused;
}

void captureVst3Records(const juce::AudioPluginInstance& instance, DeviceInfo& device) {
    applyVst3Records(device, readVst3Preset(instance));
}

std::optional<ExternalPluginSnapshot> captureExternalPluginState(
    juce::AudioPluginInstance& instance) {
    juce::MemoryBlock chunk;
    ExternalPluginSnapshot snapshot;

    // One suspension for all three reads: the chunk, the parameters and the
    // preset must describe the same moment.
    bool described = true;
    {
        const ScopedSuspend held(instance);

        try {
            instance.getStateInformation(chunk);
            snapshot.parameters = snapshotHostParameters(instance);
            snapshot.portable = readVst3Preset(instance);
        } catch (...) {
            described = false;
        }
    }

    if (!described)
        return std::nullopt;

    // Absent rather than empty for a plugin with nothing to say, which is what
    // the fork writes for one: it removes the property rather than storing a
    // zero-length chunk, and a project that stored one would come back through
    // decodeSavedChunk() as a baseline anyway.
    if (chunk.getSize() > 0)
        snapshot.pluginState = chunk.toBase64Encoding();

    return snapshot;
}

void applyCapturedPluginState(DeviceInfo& device, const ExternalPluginSnapshot& snapshot) {
    device.pluginState = snapshot.pluginState;
    applyRestoredParameters(device, snapshot.parameters);
    applyVst3Records(device, snapshot.portable);
}

}  // namespace magda
