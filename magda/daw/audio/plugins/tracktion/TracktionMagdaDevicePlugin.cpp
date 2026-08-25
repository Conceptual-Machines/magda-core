#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "core/ParameterUtils.hpp"

namespace magda::daw::audio::tracktion_adapter {

namespace {

DeviceProperties propertiesForRequiredDevice(const std::unique_ptr<MagdaDevice>& device) {
    jassert(device != nullptr);
    return device->properties();
}

/**
 * The edit's tempo sequence, read through the SDK's tempo contract.
 *
 * Queried on the audio thread, which is where the compiled devices that need a
 * BPM have always queried it: the tempo-synced effects each called
 * `edit.tempoSequence.getBpmAt()` from their own applyToBuffer() before they
 * were ported (#2192). This changes who holds the reference, not when the call
 * happens. A snapshot taken on the message thread would be the better contract
 * and belongs with the state slice rather than with the device ports.
 */
class TracktionTempoMapView final : public DeviceTempoMap {
  public:
    explicit TracktionTempoMapView(te::TempoSequence& tempoSequence)
        : tempoSequence_(tempoSequence) {}

    double beatsAtSeconds(double seconds) const override {
        return tempoSequence_.toBeats(tracktion::TimePosition::fromSeconds(seconds)).inBeats();
    }

    double bpmAtSeconds(double seconds) const override {
        return tempoSequence_.getBpmAt(tracktion::TimePosition::fromSeconds(seconds));
    }

  private:
    te::TempoSequence& tempoSequence_;
};

class TracktionMidiBufferView final : public DeviceMidiBuffer {
  public:
    explicit TracktionMidiBufferView(te::MidiMessageArray& midi) : midi_(midi) {}

    int size() const override {
        return midi_.size();
    }

    const juce::MidiMessage& message(int index) const override {
        return midi_[index];
    }

    std::uint32_t sourceId(int index) const override {
        return static_cast<std::uint32_t>(midi_[index].mpeSourceID);
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

}  // namespace

TracktionMagdaDevicePlugin::TracktionMagdaDevicePlugin(const te::PluginCreationInfo& info,
                                                       std::unique_ptr<MagdaDevice> device)
    : te::Plugin(info),
      device_(std::move(device)),
      deviceHandle_(std::make_shared<MagdaDevice*>(device_.get())),
      properties_(propertiesForRequiredDevice(device_)) {
    buildParameters();
    device_->restoreState(state);
}

TracktionMagdaDevicePlugin::~TracktionMagdaDevicePlugin() {
    *deviceHandle_ = nullptr;
    notifyListenersOfDeletion();
    for (auto& parameter : parameters_)
        if (parameter)
            parameter->detachFromCurrentValue();
}

juce::String TracktionMagdaDevicePlugin::getName() const {
    return properties_.name;
}

juce::String TracktionMagdaDevicePlugin::getPluginType() {
    return properties_.pluginId;
}

juce::String TracktionMagdaDevicePlugin::getShortName(int) {
    return properties_.shortName.isEmpty() ? properties_.name : properties_.shortName;
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
    // Parameters first. A device that seeds smoothing state from one in reset()
    // -- the sidechain sets its gain follower to wherever the duck currently
    // sits -- would otherwise read whatever it was constructed with, because
    // nothing has pushed the host's values into it yet.
    syncParametersToDevice();
    device_->reset();
}

void TracktionMagdaDevicePlugin::applyToBuffer(const te::PluginRenderContext& context) {
    syncParametersToDevice();

    std::optional<TracktionMidiBufferView> midi;
    if (context.bufferForMidiMessages != nullptr)
        midi.emplace(*context.bufferForMidiMessages);

    TracktionTempoMapView tempoMap{edit.tempoSequence};

    // The fork appends the key to the plugin's own channels, so the device's
    // declared width is where it starts.
    const int sidechainChannel =
        getSidechainSourceID().isValid()
            ? (properties_.outputChannelCount > 0 ? properties_.outputChannelCount : 2)
            : -1;

    DeviceProcessContext deviceContext{
        .audio = context.destBuffer,
        .sidechainInputChannel = sidechainChannel,
        .midi = midi ? &*midi : nullptr,
        .tempoMap = &tempoMap,
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
    return properties_.takesMidiInput;
}

bool TracktionMagdaDevicePlugin::takesAudioInput() {
    return properties_.takesAudioInput;
}

bool TracktionMagdaDevicePlugin::isSynth() {
    return properties_.isSynth;
}

bool TracktionMagdaDevicePlugin::producesAudioWhenNoAudioInput() {
    return properties_.producesAudioWithoutInput;
}

bool TracktionMagdaDevicePlugin::canSidechain() {
    // Live, not cached. Most devices' properties are fixed for their lifetime,
    // but the runtime Faust device recompiles to a different channel width and
    // the host has to follow it. Dropping the extra inputs also drops the route
    // that fed them, which nothing downstream would otherwise clear.
    const auto live = device_->properties();
    if (!live.canSidechain && getSidechainSourceID().isValid())
        setSidechainSourceID({});
    return live.canSidechain;
}

int TracktionMagdaDevicePlugin::getNumOutputChannelsGivenInputs(int numInputChannels) {
    const auto outputs = device_->properties().outputChannelCount;
    return outputs > 0 ? outputs : numInputChannels;
}

void TracktionMagdaDevicePlugin::getChannelNames(juce::StringArray* inputs,
                                                 juce::StringArray* outputs) {
    // The base answers first, and a declared width replaces that side and only
    // that side. A device that names its inputs and not its outputs (or the
    // other way round) still gets the host's answer for the half it left alone:
    // reporting zero channels there would tell the model the device is not
    // connected to the bus at all.
    te::Plugin::getChannelNames(inputs, outputs);

    const auto live = device_->properties();

    // Inputs past the output width are the key, which is the SDK's sidechain
    // layout. A stereo key is named per side; a single one is just the key.
    const int keyChannels = std::max(0, live.inputChannelCount - live.outputChannelCount);
    const auto name = [&](int index) {
        if (live.outputChannelCount > 0 && index >= live.outputChannelCount) {
            if (keyChannels < 2)
                return juce::String("Sidechain");
            return juce::String(index == live.outputChannelCount ? "Sidechain Left"
                                                                 : "Sidechain Right");
        }
        return juce::String(index == 0 ? "Left" : "Right");
    };

    if (inputs != nullptr && live.inputChannelCount > 0) {
        inputs->clear();
        for (int index = 0; index < live.inputChannelCount; ++index)
            inputs->add(name(index));
    }

    if (outputs != nullptr && live.outputChannelCount > 0) {
        outputs->clear();
        for (int index = 0; index < live.outputChannelCount; ++index)
            outputs->add(name(index));
    }
}

double TracktionMagdaDevicePlugin::getLatencySeconds() {
    return properties_.latencySeconds;
}

double TracktionMagdaDevicePlugin::getTailLength() const {
    return properties_.tailLengthSeconds;
}

te::AutomatableParameter* TracktionMagdaDevicePlugin::parameterForDeviceSlot(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(parameters_.size()))
        return nullptr;
    return parameters_[static_cast<std::size_t>(slotIndex)].get();
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
        const auto id = info.stableId.isNotEmpty()
                            ? info.stableId
                            : getPluginType() + "_param_" + juce::String(stableIndex);
        const float defaultValue = ParameterUtils::realToNormalized(info.defaultValue, info);

        auto cachedValue = std::make_unique<juce::CachedValue<float>>();
        cachedValue->referTo(state, juce::Identifier(id), getUndoManager(), defaultValue);

        // Read from the device rather than from a copy taken here. Almost every
        // device's parameter metadata is fixed for its lifetime and the two are
        // the same thing, but the runtime Faust device rebinds its pool on every
        // compile: a captured copy would leave the host formatting, parsing and
        // scaling the slot against a patch that is no longer loaded.
        const auto live = [handle = deviceHandle_, index, info]() {
            auto* device = *handle;
            return device != nullptr ? device->parameterInfo(index) : info;
        };

        auto parameter = addParam(
            id, info.name, {0.0f, 1.0f},
            [live](float value) {
                const auto current = live();
                return ParameterUtils::formatValue(ParameterUtils::normalizedToReal(value, current),
                                                   current);
            },
            [live](const juce::String& text) {
                const auto current = live();
                const auto value = ParameterUtils::parseValue(text, current);
                return value ? ParameterUtils::realToNormalized(*value, current) : 0.0f;
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
