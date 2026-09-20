#include "AudioIOService.hpp"

#include <algorithm>
#include <iterator>

#include "HardwareRouteNames.hpp"
#include "TracktionAudioSettings.hpp"

namespace magda {

namespace {

/// What a fresh configuration opens, and what replaces an output that is gone.
const std::vector<int> kStereoOut{0, 1};

std::vector<int> within(const std::vector<int>& channels, int count) {
    std::vector<int> kept;
    std::ranges::copy_if(channels, std::back_inserter(kept),
                         [count](int channel) { return channel >= 0 && channel < count; });
    return kept;
}

juce::BigInteger maskOf(const std::vector<int>& channels) {
    juce::BigInteger mask;
    for (const auto channel : channels)
        mask.setBit(channel);
    return mask;
}

bool hasInterfaces(const juce::AudioIODeviceType& backend) {
    return !backend.getDeviceNames(true).isEmpty() || !backend.getDeviceNames(false).isEmpty();
}

void log(const juce::String& message) {
    juce::Logger::writeToLog("[audio-io] " + message);
}

}  // namespace

AudioIOService::AudioIOService() : AudioIOService({}, tracktionSettingsFile()) {}

AudioIOService::AudioIOService(std::vector<std::unique_ptr<juce::AudioIODeviceType>> backends,
                               juce::File tracktionSettings)
    : tracktionSettings_(std::move(tracktionSettings)) {
    // Before the first scan: JUCE creates the platform's backends only when it has none.
    for (auto& backend : backends)
        manager_.addAudioDeviceType(std::move(backend));

    manager_.addChangeListener(this);
}

AudioIOService::~AudioIOService() {
    manager_.removeChangeListener(this);
    manager_.closeAudioDevice();
}

void AudioIOService::open() {
    auto& config = Config::getInstance();
    auto wanted = config.getAudioIO();
    const auto migrating = !wanted.has_value();
    if (migrating)
        wanted = readTracktionAudioSettings(tracktionSettings_);

    const auto fitted = fit(wanted);
    if (const auto error = openFitted(fitted); error.isNotEmpty())
        log("could not open " + juce::String(fitted.outputInterface) + " on " +
            juce::String(fitted.backend) + ": " + error);

    if (!migrating || !wanted)
        return;

    // Saved clipped to what the interface has, unless it is gone and its count unknown.
    const auto interfacesKept = fitted.backend == wanted->backend &&
                                fitted.inputInterface == wanted->inputInterface &&
                                fitted.outputInterface == wanted->outputInterface;
    config.setAudioIO(interfacesKept ? fitted : *wanted);
    config.save();
}

juce::String AudioIOService::apply(const AudioIOSettings& settings) {
    auto error = openFitted(fit(settings));
    if (error.isNotEmpty()) {
        log("could not apply " + juce::String(settings.outputInterface) + ": " + error);
        return error;
    }

    auto& config = Config::getInstance();
    config.setAudioIO(settings);
    config.save();
    return {};
}

juce::StringArray AudioIOService::getBackendNames() {
    juce::StringArray names;
    for (const auto* backend : manager_.getAvailableDeviceTypes())
        names.add(backend->getTypeName());
    return names;
}

juce::StringArray AudioIOService::getInterfaceNames(const juce::String& backend, bool inputs) {
    const auto* found = backendNamed(backend);
    return found != nullptr ? found->getDeviceNames(inputs) : juce::StringArray{};
}

bool AudioIOService::isSingleInterfaceBackend(const juce::String& backend) {
    const auto* found = backendNamed(backend);
    return found != nullptr && found->hasSeparateInputsAndOutputs() == false;
}

std::vector<double> AudioIOService::availableSampleRates() const {
    std::vector<double> rates;
    if (auto* device = manager_.getCurrentAudioDevice())
        for (const auto rate : device->getAvailableSampleRates())
            rates.push_back(rate);
    return rates;
}

std::vector<int> AudioIOService::availableBufferSizes() const {
    std::vector<int> sizes;
    if (auto* device = manager_.getCurrentAudioDevice())
        for (const auto size : device->getAvailableBufferSizes())
            sizes.push_back(size);
    return sizes;
}

AudioIOControl::Status AudioIOService::status() const {
    Status status;
    if (auto* device = manager_.getCurrentAudioDevice()) {
        status.interfaceName = device->getName();
        status.sampleRate = device->getCurrentSampleRate();
        status.bufferSize = device->getCurrentBufferSizeSamples();
    }
    status.cpuUsage = manager_.getCpuUsage();
    status.xruns = manager_.getXRunCount();
    return status;
}

juce::StringArray AudioIOService::getChannelNames(const juce::String& backend,
                                                  const juce::String& interfaceName, bool inputs) {
    // The open interface answers for itself rather than through a second instance of its driver.
    if (auto* device = manager_.getCurrentAudioDevice();
        device != nullptr && device->getTypeName() == backend) {
        const auto setup = manager_.getAudioDeviceSetup();
        if ((inputs ? setup.inputDeviceName : setup.outputDeviceName) == interfaceName)
            return inputs ? device->getInputChannelNames() : device->getOutputChannelNames();
    }

    auto* found = backendNamed(backend);
    if (found == nullptr || interfaceName.isEmpty())
        return {};

    const std::unique_ptr<juce::AudioIODevice> probe(found->createDevice(
        inputs ? juce::String() : interfaceName, inputs ? interfaceName : juce::String()));
    if (probe == nullptr)
        return {};

    return inputs ? probe->getInputChannelNames() : probe->getOutputChannelNames();
}

AudioIOService::ActiveConfiguration AudioIOService::getActiveConfiguration() const {
    auto* device = manager_.getCurrentAudioDevice();
    if (device == nullptr || !device->isOpen())
        return {};

    const auto setup = manager_.getAudioDeviceSetup();
    return {.backend = device->getTypeName(),
            .inputInterface = setup.inputDeviceName,
            .outputInterface = setup.outputDeviceName,
            .sampleRate = device->getCurrentSampleRate(),
            .bufferSize = device->getCurrentBufferSizeSamples(),
            .inputChannels = device->getActiveInputChannels(),
            .outputChannels = device->getActiveOutputChannels(),
            .inputChannelNames = device->getInputChannelNames(),
            .outputChannelNames = device->getOutputChannelNames(),
            .inputLatencySamples = device->getInputLatencyInSamples()};
}

AudioIOSettings AudioIOService::openSettings() const {
    const auto active = getActiveConfiguration();
    const auto channelsOf = [](const juce::BigInteger& mask) {
        std::vector<int> channels;
        for (auto bit = mask.findNextSetBit(0); bit >= 0; bit = mask.findNextSetBit(bit + 1))
            channels.push_back(bit);
        return channels;
    };
    return {.backend = active.backend.toStdString(),
            .inputInterface = active.inputInterface.toStdString(),
            .outputInterface = active.outputInterface.toStdString(),
            .sampleRate = active.sampleRate,
            .bufferSize = active.bufferSize,
            .inputChannels = channelsOf(active.inputChannels),
            .outputChannels = channelsOf(active.outputChannels)};
}

AudioIOSettings AudioIOService::chosen() const {
    const auto& saved = Config::getInstance().getAudioIO();
    return saved.has_value() ? *saved : openSettings();
}

bool AudioIOService::isOpen() const {
    return getActiveConfiguration().backend.isNotEmpty();
}

HardwareChannels::Direction AudioIOService::inputs() const {
    const auto active = getActiveConfiguration();
    return {.open = active.inputChannels,
            .channelNames = active.inputChannelNames,
            .routeNames =
                routeNamesByChannel(active.inputChannelNames, active.inputChannels, true)};
}

HardwareChannels::Direction AudioIOService::outputs() const {
    const auto active = getActiveConfiguration();
    return {.open = active.outputChannels,
            .channelNames = active.outputChannelNames,
            .routeNames =
                routeNamesByChannel(active.outputChannelNames, active.outputChannels, false)};
}

juce::AudioIODeviceType* AudioIOService::backendNamed(const juce::String& name) {
    for (auto* backend : manager_.getAvailableDeviceTypes())
        if (backend->getTypeName() == name)
            return backend;
    return nullptr;
}

juce::AudioIODeviceType* AudioIOService::defaultBackend() {
    for (auto* backend : manager_.getAvailableDeviceTypes())
        if (hasInterfaces(*backend))
            return backend;
    return nullptr;
}

AudioIOSettings AudioIOService::fit(const std::optional<AudioIOSettings>& wanted) {
    auto* savedBackend = wanted ? backendNamed(wanted->backend) : nullptr;
    const auto* saved =
        savedBackend != nullptr && hasInterfaces(*savedBackend) ? &*wanted : nullptr;
    auto* backend = saved != nullptr ? savedBackend : defaultBackend();
    if (backend == nullptr)
        return {};

    AudioIOSettings fitted;
    fitted.backend = backend->getTypeName().toStdString();
    if (wanted) {
        fitted.sampleRate = wanted->sampleRate;
        fitted.bufferSize = wanted->bufferSize;
    }

    const auto present = [&](const std::string& name, bool inputs) {
        return !name.empty() && backend->getDeviceNames(inputs).contains(juce::String(name));
    };
    const auto channelCount = [&](const std::string& name, bool inputs) {
        return getChannelNames(backend->getTypeName(), name, inputs).size();
    };

    if (saved != nullptr && present(saved->outputInterface, false)) {
        fitted.outputInterface = saved->outputInterface;
        fitted.outputChannels =
            within(saved->outputChannels, channelCount(saved->outputInterface, false));
    } else if (saved == nullptr || !saved->outputInterface.empty()) {
        // Fresh, or the saved output is gone: the backend's default, at stereo.
        const auto outputs = backend->getDeviceNames(false);
        if (!outputs.isEmpty()) {
            const auto name = outputs[std::max(0, backend->getDefaultDeviceIndex(false))];
            fitted.outputInterface = name.toStdString();
            fitted.outputChannels = within(kStereoOut, channelCount(fitted.outputInterface, false));
        }
    }

    if (saved != nullptr && present(saved->inputInterface, true)) {
        fitted.inputInterface = saved->inputInterface;
        fitted.inputChannels =
            within(saved->inputChannels, channelCount(saved->inputInterface, true));
    }

    return fitted;
}

juce::String AudioIOService::openFitted(const AudioIOSettings& fitted) {
    // The XML path is the one JUCE call that picks a backend without first opening that
    // backend's default interface. Whatever MIDI the manager holds is carried over.
    auto xml = manager_.createStateXml();
    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement>("DEVICESETUP");

    // An interface with no channels selected is not opened at all.
    const auto inputs = maskOf(fitted.inputChannels);
    const auto outputs = maskOf(fitted.outputChannels);
    xml->removeAttribute("audioDeviceName");
    xml->setAttribute("deviceType", juce::String(fitted.backend));
    xml->setAttribute("audioInputDeviceName",
                      inputs.isZero() ? juce::String() : juce::String(fitted.inputInterface));
    xml->setAttribute("audioOutputDeviceName",
                      outputs.isZero() ? juce::String() : juce::String(fitted.outputInterface));
    xml->setAttribute("audioDeviceRate", fitted.sampleRate);
    xml->setAttribute("audioDeviceBufferSize", fitted.bufferSize);
    xml->setAttribute("audioDeviceInChans", inputs.toString(2));
    xml->setAttribute("audioDeviceOutChans", outputs.toString(2));

    // JUCE compares setups without the backend, so a same-named interface on another backend
    // (Windows Audio's shared and exclusive modes) would keep the old device running.
    if (auto* device = manager_.getCurrentAudioDevice();
        device != nullptr && device->getTypeName() != juce::String(fitted.backend))
        manager_.closeAudioDevice();

    auto error = manager_.initialise(0, 0, xml.get(), false);
    if (const auto active = getActiveConfiguration(); active.backend.isEmpty()) {
        log("no interface open");
    } else {
        log("opened " + active.outputInterface + " / " + active.inputInterface + " on " +
            active.backend + ": " + juce::String(active.outputChannels.countNumberOfSetBits()) +
            " out, " + juce::String(active.inputChannels.countNumberOfSetBits()) + " in, " +
            juce::String(active.sampleRate) + " Hz, " + juce::String(active.bufferSize) +
            " samples");
    }
    return error;
}

void AudioIOService::changeListenerCallback(juce::ChangeBroadcaster*) {
    notifyChanged();
}

}  // namespace magda
