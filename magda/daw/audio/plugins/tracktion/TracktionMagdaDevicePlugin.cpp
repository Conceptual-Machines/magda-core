#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "core/ParameterUtils.hpp"

namespace magda::daw::audio::tracktion_adapter {

namespace {

class TracktionMidiBufferView final : public DeviceMidiBuffer {
  public:
    explicit TracktionMidiBufferView(te::MidiMessageArray& midi) : midi_(midi) {}

    int size() const override {
        return midi_.size();
    }

    DeviceMidiEvent event(int index) const override {
        const auto& event = midi_[index];
        return {
            .message = event,
            .sourceId = static_cast<std::uint32_t>(event.mpeSourceID),
        };
    }

    void setEvent(int index, DeviceMidiEvent event) override {
        midi_[index] = te::MidiMessageWithSource(std::move(event.message),
                                                 static_cast<te::MPESourceID>(event.sourceId));
    }

    void removeEvent(int index) override {
        midi_.remove(index);
    }

    void addEvent(DeviceMidiEvent event) override {
        midi_.addMidiMessage(std::move(event.message),
                             static_cast<te::MPESourceID>(event.sourceId));
    }

    void clear() override {
        midi_.clear();
    }

    void sortByTimestamp() override {
        midi_.sortByTimestamp();
    }

    bool isAllNotesOff() const override {
        return midi_.isAllNotesOff;
    }

    void setAllNotesOff(bool allNotesOff) override {
        midi_.isAllNotesOff = allNotesOff;
    }

  private:
    te::MidiMessageArray& midi_;
};

juce::String asJuceString(std::string_view text) {
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

TracktionMagdaDevicePlugin::TracktionMagdaDevicePlugin(const te::PluginCreationInfo& info,
                                                       std::unique_ptr<MagdaDevice> device)
    : te::Plugin(info), device_(std::move(device)) {
    jassert(device_ != nullptr);
    buildParameters();
}

TracktionMagdaDevicePlugin::~TracktionMagdaDevicePlugin() {
    notifyListenersOfDeletion();
    for (auto& parameter : parameters_)
        if (parameter)
            parameter->detachFromCurrentValue();
}

juce::String TracktionMagdaDevicePlugin::getName() const {
    return asJuceString(device_->properties().name);
}

juce::String TracktionMagdaDevicePlugin::getPluginType() {
    return asJuceString(device_->properties().pluginId);
}

juce::String TracktionMagdaDevicePlugin::getShortName(int) {
    const auto properties = device_->properties();
    return asJuceString(properties.shortName.empty() ? properties.name : properties.shortName);
}

juce::String TracktionMagdaDevicePlugin::getSelectableDescription() {
    return getName();
}

void TracktionMagdaDevicePlugin::initialise(const te::PluginInitialisationInfo& info) {
    device_->prepare({
        .sampleRate = info.sampleRate,
        .maximumBlockSize = info.blockSizeSamples,
    });
}

void TracktionMagdaDevicePlugin::deinitialise() {
    device_->release();
}

void TracktionMagdaDevicePlugin::reset() {
    device_->reset();
}

void TracktionMagdaDevicePlugin::applyToBuffer(const te::PluginRenderContext& context) {
    syncParametersToDevice();

    std::optional<TracktionMidiBufferView> midi;
    if (context.bufferForMidiMessages != nullptr)
        midi.emplace(*context.bufferForMidiMessages);

    DeviceProcessContext deviceContext{
        .audio = context.destBuffer,
        .midi = midi ? &*midi : nullptr,
        .startSample = context.bufferStartSample,
        .numSamples = context.bufferNumSamples,
        .midiTimeOffsetSeconds = context.midiBufferOffset,
        .timelineStartSeconds = context.editTime.getStart().inSeconds(),
        .timelineEndSeconds = context.editTime.getEnd().inSeconds(),
        .isPlaying = context.isPlaying,
        .isScrubbing = context.isScrubbing,
        .isRendering = context.isRendering,
    };
    device_->process(deviceContext);
}

bool TracktionMagdaDevicePlugin::takesMidiInput() {
    return device_->properties().takesMidiInput;
}

bool TracktionMagdaDevicePlugin::takesAudioInput() {
    return device_->properties().takesAudioInput;
}

bool TracktionMagdaDevicePlugin::isSynth() {
    return device_->properties().isSynth;
}

bool TracktionMagdaDevicePlugin::producesAudioWhenNoAudioInput() {
    return device_->properties().producesAudioWithoutInput;
}

bool TracktionMagdaDevicePlugin::canSidechain() {
    return device_->properties().canSidechain;
}

double TracktionMagdaDevicePlugin::getLatencySeconds() {
    return device_->properties().latencySeconds;
}

double TracktionMagdaDevicePlugin::getTailLength() const {
    return device_->properties().tailLengthSeconds;
}

void TracktionMagdaDevicePlugin::flushPluginStateToValueTree() {
    device_->flushState(state);
    te::Plugin::flushPluginStateToValueTree();
}

void TracktionMagdaDevicePlugin::restorePluginStateFromValueTree(
    const juce::ValueTree& restoredState) {
    for (auto& parameterValue : parameterValues_)
        tracktion::copyPropertiesToCachedValues(restoredState, *parameterValue);
    syncParametersToDevice();
    device_->restoreState(restoredState);
}

void TracktionMagdaDevicePlugin::buildParameters() {
    const int count = std::max(0, device_->parameterCount());
    parameterValues_.reserve(static_cast<std::size_t>(count));
    parameters_.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index) {
        auto info = device_->parameterInfo(index);
        const int stableIndex = info.paramIndex >= 0 ? info.paramIndex : index;
        const auto id = getPluginType() + "_param_" + juce::String(stableIndex);
        const float defaultValue = ParameterUtils::realToNormalized(info.defaultValue, info);

        auto cachedValue = std::make_unique<juce::CachedValue<float>>();
        cachedValue->referTo(state, juce::Identifier(id), getUndoManager(), defaultValue);

        auto parameter = addParam(
            id, info.name, {0.0f, 1.0f},
            [info](float value) {
                return ParameterUtils::formatValue(ParameterUtils::normalizedToReal(value, info),
                                                   info);
            },
            [info](const juce::String& text) {
                const auto value = ParameterUtils::parseValue(text, info);
                return value ? ParameterUtils::realToNormalized(*value, info) : 0.0f;
            });
        parameter->attachToCurrentValue(*cachedValue);

        parameterValues_.push_back(std::move(cachedValue));
        parameters_.push_back(parameter);
    }

    syncParametersToDevice();
}

void TracktionMagdaDevicePlugin::syncParametersToDevice() {
    for (int index = 0; index < static_cast<int>(parameters_.size()); ++index)
        if (parameters_[static_cast<std::size_t>(index)])
            device_->setParameterValue(
                index, parameters_[static_cast<std::size_t>(index)]->getCurrentValue());
}

}  // namespace magda::daw::audio::tracktion_adapter
