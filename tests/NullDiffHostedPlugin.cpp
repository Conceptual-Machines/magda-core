#include "NullDiffHostedPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace magda::nulldiff {

namespace {

/// A note-on with a velocity of zero is a note-off, which is the one thing a
/// device counting note-ons has to know about MIDI.
bool isNoteOn(const juce::MidiMessage& message) {
    return message.isNoteOn();
}

/// What each role is called, which is what a project saves and what the scan
/// holds. One name per role, because a project names a plugin rather than a
/// mode.
const char* roleName(HostedRole role) {
    switch (role) {
        case HostedRole::Polarity:
            return "Null Diff Polarity";
        case HostedRole::Narrow:
            return "Null Diff Narrow";
        case HostedRole::Latency:
            return "Null Diff Latency";
        case HostedRole::Key:
            return "Null Diff Key";
        case HostedRole::Transport:
            return "Null Diff Transport";
        case HostedRole::Instrument:
            return "Null Diff Voice";
        case HostedRole::InstrumentMidiOut:
            return "Null Diff Relay";
        case HostedRole::Echo:
            return "Null Diff Echo";
    }

    return "Null Diff";
}

/// The identifier a scan would have recorded. A path shape rather than a real
/// path: nothing opens it, and the lookup's file passes compare it as a string.
juce::String roleIdentifier(HostedRole role) {
    return juce::String("nulldiff://") + roleName(role);
}

/// The number a description carries as its own identity. Stable across runs and
/// distinct per role, since two roles sharing one would resolve to whichever the
/// list held first.
int roleUid(HostedRole role) {
    return 0x4E44'0000 + static_cast<int>(role);
}

bool roleIsInstrument(HostedRole role) {
    return role == HostedRole::Instrument || role == HostedRole::InstrumentMidiOut;
}

bool roleAcceptsMidi(HostedRole role) {
    return roleIsInstrument(role) || role == HostedRole::Echo;
}

bool roleProducesMidi(HostedRole role) {
    return role == HostedRole::InstrumentMidiOut;
}

/// How many channels of chain audio the plugin reads, and how many it writes.
/// An instrument reads none: both engines route audio around one rather than
/// into it.
int roleInputChannels(HostedRole role) {
    switch (role) {
        case HostedRole::Narrow:
            return 1;
        case HostedRole::Instrument:
        case HostedRole::InstrumentMidiOut:
            return 0;
        default:
            return 2;
    }
}

int roleOutputChannels(HostedRole role) {
    return role == HostedRole::Narrow ? 1 : 2;
}

/**
 * @brief The plugin itself: one class, one behaviour per role.
 *
 * Everything it does is a function of the block it was handed and the position
 * it was told about, with one exception that is state by definition -- the
 * delay line the latency role reports. Nothing else accumulates, so the same
 * timeline renders the same samples at every block size, and a case built on it
 * can be held to bit identity across the sizes the invariance gate renders at
 * (#2078) rather than buying the epsilon a project hosting a plugin is entitled
 * to.
 */
class HostedPlugin final : public juce::AudioPluginInstance {
  public:
    explicit HostedPlugin(HostedRole role)
        : juce::AudioPluginInstance(busesFor(role)), role_(role) {
        if (role_ == HostedRole::Latency)
            setLatencySamples(kHostedLatencySamples);
    }

    const juce::String getName() const override {
        return roleName(role_);
    }

    void fillInPluginDescription(juce::PluginDescription& description) const override {
        description = hostedDescription(role_);
    }

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        juce::ignoreUnused(maximumExpectedSamplesPerBlock);

        // Cleared rather than resized-and-kept: a render is entitled to start
        // from silence, and a delay line carrying the last case's tail would
        // put one case's material into another's first thirty samples.
        delay_.assign(static_cast<std::size_t>(std::max(1, kHostedLatencySamples)) * 2, 0.0f);
        delayWritePosition_ = 0;

        if (role_ == HostedRole::Latency)
            setLatencySamples(kHostedLatencySamples);
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        const auto width = [](const juce::AudioChannelSet& set) { return set.size(); };

        if (width(layouts.getMainOutputChannelSet()) != roleOutputChannels(role_))
            return false;

        if (roleIsInstrument(role_))
            return layouts.inputBuses.isEmpty();

        if (width(layouts.getMainInputChannelSet()) != roleInputChannels(role_))
            return false;

        if (role_ == HostedRole::Key)
            return layouts.inputBuses.size() == 2 && width(layouts.getChannelSet(true, 1)) == 2;

        return layouts.inputBuses.size() == 1;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        const auto numSamples = buffer.getNumSamples();

        switch (role_) {
            case HostedRole::Polarity:
                for (int channel = 0; channel < roleOutputChannels(role_); ++channel)
                    if (channel < buffer.getNumChannels())
                        buffer.applyGain(channel, 0, numSamples, -1.0f);
                break;

            case HostedRole::Narrow:
                // Identity. What a mono case measures is the fold and the
                // spread the host puts either side of this call.
                break;

            case HostedRole::Latency:
                processDelay(buffer);
                break;

            case HostedRole::Key:
                processKey(buffer);
                break;

            case HostedRole::Transport:
                processTransport(buffer);
                break;

            case HostedRole::Instrument:
            case HostedRole::InstrumentMidiOut:
                processVoice(buffer, midi);
                break;

            case HostedRole::Echo:
                processEcho(buffer, midi);
                break;
        }
    }

    double getTailLengthSeconds() const override {
        return 0.0;
    }

    bool acceptsMidi() const override {
        return roleAcceptsMidi(role_);
    }

    bool producesMidi() const override {
        return roleProducesMidi(role_);
    }

    bool isMidiEffect() const override {
        return false;
    }

    juce::AudioProcessorEditor* createEditor() override {
        return nullptr;
    }

    bool hasEditor() const override {
        return false;
    }

    int getNumPrograms() override {
        return 1;
    }

    int getCurrentProgram() override {
        return 0;
    }

    void setCurrentProgram(int) override {}

    const juce::String getProgramName(int) override {
        return "Default";
    }

    void changeProgramName(int, const juce::String&) override {}

    /// No state of its own, and it says so by writing nothing.
    ///
    /// A plugin with a chunk would put the restore contract (#2244) in the
    /// middle of every case here, and what these cases are about is the block.
    /// What that contract does is asserted where it belongs, against a stub
    /// written for it (test_engine_external_device.cpp).
    void getStateInformation(juce::MemoryBlock& destination) override {
        destination.reset();
    }

    void setStateInformation(const void*, int) override {}

  private:
    void processDelay(juce::AudioBuffer<float>& buffer) {
        const auto numSamples = buffer.getNumSamples();
        const auto length = kHostedLatencySamples;
        const auto channels = std::min(buffer.getNumChannels(), 2);

        for (int sample = 0; sample < numSamples; ++sample) {
            const auto slot = static_cast<std::size_t>(delayWritePosition_);

            for (int channel = 0; channel < channels; ++channel) {
                auto& stored = delay_[(slot * 2) + static_cast<std::size_t>(channel)];
                const auto input = buffer.getSample(channel, sample);
                buffer.setSample(channel, sample, stored);
                stored = input;
            }

            delayWritePosition_ = (delayWritePosition_ + 1) % length;
        }
    }

    void processKey(juce::AudioBuffer<float>& buffer) {
        auto output = getBusBuffer(buffer, false, 0);

        if (getBusCount(true) < 2 || !getBus(true, 1)->isEnabled()) {
            // A key bus the host did not give us. Silence rather than the main
            // input, so a case reads it as the key never arriving instead of as
            // the plugin having been bypassed.
            output.clear();
            return;
        }

        const auto key = getBusBuffer(buffer, true, 1);

        for (int channel = 0; channel < output.getNumChannels(); ++channel) {
            const auto source = std::min(channel, key.getNumChannels() - 1);
            if (source < 0) {
                output.clear(channel, 0, output.getNumSamples());
                continue;
            }

            output.copyFrom(channel, 0, key, source, 0, output.getNumSamples());
        }
    }

    void processTransport(juce::AudioBuffer<float>& buffer) {
        const auto numSamples = buffer.getNumSamples();

        auto* playHead = getPlayHead();
        const auto position = playHead != nullptr
                                  ? playHead->getPosition()
                                  : juce::Optional<juce::AudioPlayHead::PositionInfo>{};

        if (!position.hasValue()) {
            // Nothing to render from. Silence is the right answer and a loud
            // one: a leg that told the plugin nothing renders nothing, and the
            // case fails against the leg that did.
            buffer.clear();
            return;
        }

        const auto ppq = position->getPpqPosition().orFallback(0.0);
        const auto bpm = position->getBpm().orFallback(120.0);
        const auto perSample = bpm / 60.0 / sampleRate_;

        for (int sample = 0; sample < numSamples; ++sample) {
            const auto beats = ppq + perSample * static_cast<double>(sample);
            const auto value = static_cast<float>(
                kHostedTransportLevel *
                std::sin(2.0 * juce::MathConstants<double>::pi * beats / kHostedTransportBeats));

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample(channel, sample, value);
        }
    }

    void processVoice(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
        const auto numSamples = buffer.getNumSamples();
        buffer.clear();

        juce::MidiBuffer produced;

        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (!isNoteOn(message))
                continue;

            const auto sample = std::clamp(metadata.samplePosition, 0, std::max(0, numSamples - 1));
            const auto level = static_cast<float>(message.getVelocity()) / 127.0f;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.addSample(channel, sample, level);

            if (role_ == HostedRole::InstrumentMidiOut) {
                // An octave up at half the velocity, so what reaches the next
                // device is this plugin's own message and could not be the raw
                // input wearing its name.
                produced.addEvent(
                    juce::MidiMessage::noteOn(message.getChannel(), message.getNoteNumber() + 12,
                                              static_cast<juce::uint8>(message.getVelocity() / 2)),
                    sample);
            }
        }

        // What the plugin says, rather than what it was handed. A plugin that
        // left its input in the buffer would be a passthrough claiming to be a
        // source, and the case behind it could not tell the two apart.
        midi.swapWith(produced);
    }

    void processEcho(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
        const auto numSamples = buffer.getNumSamples();

        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (!isNoteOn(message))
                continue;

            const auto sample = std::clamp(metadata.samplePosition, 0, std::max(0, numSamples - 1));
            const auto level =
                kHostedEchoScale * static_cast<float>(message.getVelocity()) / 127.0f;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.addSample(channel, sample, level);
        }
    }

    /// The buses each role declares.
    ///
    /// A member because BusesProperties is the processor's own type, and a
    /// static one because the constructor passes it to the base before there is
    /// an instance to ask.
    static BusesProperties busesFor(HostedRole role) {
        const auto main = juce::AudioChannelSet::stereo();
        const auto mono = juce::AudioChannelSet::mono();

        switch (role) {
            case HostedRole::Narrow:
                return BusesProperties().withInput("In", mono, true).withOutput("Out", mono, true);

            case HostedRole::Key:
                // The sidechain is a second input bus, which is what makes the
                // fork call the plugin sidechainable at all: Plugin::canSidechain
                // asks whether it has more input channels than a track has.
                return BusesProperties()
                    .withInput("In", main, true)
                    .withInput("Sidechain", main, true)
                    .withOutput("Out", main, true);

            case HostedRole::Instrument:
            case HostedRole::InstrumentMidiOut:
                return BusesProperties().withOutput("Out", main, true);

            default:
                return BusesProperties().withInput("In", main, true).withOutput("Out", main, true);
        }
    }

    HostedRole role_;
    double sampleRate_ = 44100.0;
    std::vector<float> delay_;
    int delayWritePosition_ = 0;
};

/**
 * @brief The format that makes those instances, as far as either engine knows.
 *
 * It scans nothing and finds nothing on disk, which is the whole point: what it
 * publishes is in the binary, so a case that hosts one of these runs on a
 * machine with no plugins installed and on CI, where every real plugin is
 * absent.
 *
 * Creation is synchronous, and it says so. The fork asks a format whether it
 * needs an unblocked message thread and takes the async path when it does
 * (ExternalPlugin::requiresAsyncInstantiation), which would leave a leg
 * rendering a project whose plugins had not arrived yet.
 */
class HostedFormat final : public juce::AudioPluginFormat {
  public:
    juce::String getName() const override {
        return kHostedFormatName;
    }

    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                             const juce::String& fileOrIdentifier) override {
        for (const auto role : kHostedRoles) {
            auto description = hostedDescription(role);
            if (description.fileOrIdentifier == fileOrIdentifier)
                results.add(new juce::PluginDescription(description));
        }
    }

    bool fileMightContainThisPluginType(const juce::String& fileOrIdentifier) override {
        return fileOrIdentifier.startsWith("nulldiff://");
    }

    juce::String getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier) override {
        return fileOrIdentifier.fromLastOccurrenceOf("/", false, false);
    }

    bool pluginNeedsRescanning(const juce::PluginDescription&) override {
        return false;
    }

    bool doesPluginStillExist(const juce::PluginDescription&) override {
        return true;
    }

    bool canScanForPlugins() const override {
        return false;
    }

    bool isTrivialToScan() const override {
        return true;
    }

    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath&, bool, bool) override {
        return {};
    }

    juce::FileSearchPath getDefaultLocationsToSearch() override {
        return {};
    }

    bool requiresUnblockedMessageThreadDuringCreation(
        const juce::PluginDescription&) const override {
        return false;
    }

  private:
    void createPluginInstance(const juce::PluginDescription& description, double initialSampleRate,
                              int initialBufferSize, PluginCreationCallback callback) override {
        for (const auto role : kHostedRoles) {
            if (hostedDescription(role).uniqueId != description.uniqueId)
                continue;

            auto instance = std::make_unique<HostedPlugin>(role);
            instance->setRateAndBufferSizeDetails(initialSampleRate, initialBufferSize);
            callback(std::move(instance), {});
            return;
        }

        callback(nullptr, "no null-diff plugin with that identity: " + description.name);
    }
};

}  // namespace

juce::PluginDescription hostedDescription(HostedRole role) {
    juce::PluginDescription description;
    description.name = roleName(role);
    description.descriptiveName = description.name;
    description.pluginFormatName = kHostedFormatName;
    description.category = roleIsInstrument(role) ? "Synth" : "Effect";
    description.manufacturerName = kHostedManufacturer;
    description.version = "1.0.0";
    description.fileOrIdentifier = roleIdentifier(role);
    description.isInstrument = roleIsInstrument(role);
    description.numInputChannels = roleInputChannels(role) + (role == HostedRole::Key ? 2 : 0);
    description.numOutputChannels = roleOutputChannels(role);
    description.uniqueId = roleUid(role);
    description.deprecatedUid = 0;
    return description;
}

magda::DeviceInfo hostedDevice(magda::DeviceId id, HostedRole role) {
    const auto description = hostedDescription(role);

    magda::DeviceInfo device;
    device.id = id;
    device.name = description.name;
    device.pluginId = description.createIdentifierString();
    device.manufacturer = description.manufacturerName;

    // The format enum has three values and none of them is this one. What it
    // costs is nothing: the enum decides which name the lookup's format-matching
    // passes compare, and a device carrying an identifier never reaches them
    // (ExternalPluginLookup.hpp). What it must not be is Internal, which is what
    // both the plan and the block-size gate read to tell a hosted plugin from a
    // device MAGDA wrote.
    device.format = magda::PluginFormat::VST3;
    device.uniqueId = description.createIdentifierString();
    device.fileOrIdentifier = description.fileOrIdentifier;

    device.isInstrument = description.isInstrument;
    device.deviceType =
        description.isInstrument ? magda::DeviceType::Instrument : magda::DeviceType::Effect;
    device.canReceiveMidi = roleAcceptsMidi(role);
    device.producesMidi = roleProducesMidi(role);
    device.canSidechain = role == HostedRole::Key;
    device.audioInputChannels = roleInputChannels(role);
    device.audioOutputChannels = roleOutputChannels(role);

    return device;
}

void setHostedMix(magda::DeviceInfo& device, float dry, float wet) {
    const auto level = [](int index, const char* name, magda::WrapperRole role, float value) {
        magda::ParameterInfo info;
        info.paramIndex = index;
        info.stableId = name;
        info.name = name;
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = index == 0 ? 0.0f : 1.0f;
        info.currentValue = value;
        info.teMinValue = 0.0f;
        info.teMaxValue = 1.0f;
        info.scale = magda::ParameterScale::Linear;
        info.wrapperRole = role;
        return info;
    };

    device.wrapperParameters.clear();
    device.wrapperParameters.push_back(level(0, "Dry Level", magda::WrapperRole::DryGain, dry));
    device.wrapperParameters.push_back(level(1, "Wet Level", magda::WrapperRole::WetGain, wet));
}

void installHostedPlugins(juce::AudioPluginFormatManager& formats,
                          juce::KnownPluginList& knownPlugins) {
    auto alreadyRegistered = false;
    for (auto* format : formats.getFormats())
        if (format != nullptr && format->getName() == kHostedFormatName)
            alreadyRegistered = true;

    if (!alreadyRegistered)
        formats.addFormat(std::make_unique<HostedFormat>());

    // addType replaces an entry with the same identifier rather than adding a
    // second, so this is idempotent on its own.
    for (const auto role : kHostedRoles)
        knownPlugins.addType(hostedDescription(role));
}

}  // namespace magda::nulldiff
